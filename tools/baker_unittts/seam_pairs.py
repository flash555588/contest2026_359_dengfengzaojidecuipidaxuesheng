"""Real PSOLA-plus-splice inputs paired with aligned original spectral envelopes."""
from concurrent.futures import ProcessPoolExecutor
import argparse
import json
import os
from pathlib import Path
import time

import numpy as np

from .bank import digest_file
from .program_tts import AcousticStore,ProgramCosts,render_span,desired_for,track
from .seam_spectral import analyse,proximity
from .synthesize import Inventory,Span,merge_spans,bounded_join


def initialize(bank,data):
    global INV,STORE,UNITS,DATA,REFERENCES
    os.environ['OMP_NUM_THREADS']='1'
    INV=Inventory(Path(bank)/'bank.sqlite');STORE=AcousticStore(INV)
    UNITS={u.uid:u for seq in INV.units.values() for u in seq};DATA=Path(data)
    REFERENCES={r['id']:r for r in json.loads((DATA/'dataset.json').read_text())['records'] if 'skipped' not in r}
    cached={u.uid:STORE.unit(u) for u in UNITS.values()}
    STORE.unit=lambda u:cached[u.uid]


class ReferenceCosts(ProgramCosts):
    def __init__(self,plan,uid):
        super().__init__(INV,STORE,plan);self.reference=uid

    def costs(self,inventory,tokens,position,unused):
        values=super().costs(inventory,tokens,position,unused)
        for u in inventory.by_token[tokens[position]]:
            if inventory.sources[u.utterance]['origin']==self.reference:values[u.uid]=float('inf')
        return values


def example(job):
    row,seed_folder,output=job;uid=row['id'];record=REFERENCES[uid]
    try:
        path=DATA/record['path']
        if digest_file(path)!=record['sha256']:raise RuntimeError('Original features changed')
        with np.load(path,allow_pickle=False) as f:d=dict(f)
        plan=dict(tokens=[str(t) for t in d['tokens']],phones=[[str(p) for p in row if p] for row in d['phones']],
            phone_durations=d['durations'].tolist(),f0=np.exp(d['log_f0']).tolist(),voiced=d['voiced'].tolist())
        # Choose feasible complete paths using the runtime selector, rather than
        # keeping warm-up units that cannot attain the reference controls.
        spans,_=ReferenceCosts(plan,uid).select(plan['tokens'])
        units=[u for span in spans for u in INV.units[span.utterance][span.first:span.last+1]]
        chosen=np.array([u.uid for u in units],np.int64)
        if any(INV.sources[u.utterance]['origin']==uid for u in units):raise RuntimeError('Reference source leaked into input')
        for index,u in enumerate(units):
            a=STORE.unit(u);source,target,goal,voiced=desired_for(a,plan,index)
            ratio=target/source;mask=(a['voiced']>0)&(voiced>=.4)
            if ratio.min()<.5 or ratio.max()>2. or mask.sum()<3 or np.max(abs((goal-a['log_f0'])[mask]))>np.log(2):
                return dict(id=uid,split=row['split'],skipped='actual_pitch_or_phone_duration_range')
        spans=merge_spans([Span(u.utterance,u.ordinal,u.ordinal,i) for i,u in enumerate(units)])
        result=None;positions=[];seams=[];last_index=None
        for span in spans:
            audio,rate,local,details=render_span(INV,span,STORE,plan,refinements=0)
            if result is None:offset=0;result=audio
            else:
                gap=float(d['starts'][span.target_start]-d['ends'][last_index])
                if gap>.1:
                    offset=len(result)+round(gap*rate);result=np.r_[result,np.zeros(round(gap*rate),np.int16),audio]
                else:
                    _,af,_=track(result[-min(len(result),round(rate*.08)):],rate)
                    _,bf,_=track(audio[:min(len(audio),round(rate*.08))],rate)
                    left=float(np.median(af[af>0])) if np.any(af>0) else 0.
                    right=float(np.median(bf[bf>0])) if np.any(bf>0) else 0.
                    before=len(result);result,join=bounded_join(result,audio,rate,left,right)
                    offset=before-join['removed_samples'];seams.append((before-join['removed_samples']/2)/rate)
            for p in local:positions.append({**p,'output_start':p['output_start']+offset,'output_end':p['output_end']+offset})
            last_index=span.target_start+span.length-1
        if not seams:return dict(id=uid,split=row['split'],skipped='no_cross_recording_speech_seams')
        features,times=analyse(result,rate)
        # Sample-aligned original waveform loss is deliberately not used.
        source_points=[];reference_points=[]
        for i,p in enumerate(positions):
            start=p['output_start']/rate;end=p['output_end']/rate;index=p['target_index']
            if source_points and start<source_points[-1]:
                midpoint=(start+source_points[-1])/2;source_points[-1]=midpoint;start=midpoint
            source_points.extend((start,end));reference_points.extend((float(d['starts'][index]),float(d['ends'][index])))
        points=np.array(source_points);unique=np.r_[np.diff(points)>1e-8,True]
        reference_times=np.interp(times,points[unique],np.array(reference_points)[unique])
        target=np.stack([np.interp(reference_times,d['frame_times'],d['acoustic_features'][:,b]) for b in range(24)],1)
        kernel=np.ones(5)/5
        for b in range(24):
            target[:,b]=np.convolve(np.pad(target[:,b],(2,2),mode='edge'),kernel,'valid')
            features[:,b]=np.convolve(np.pad(features[:,b],(2,2),mode='edge'),kernel,'valid')
        stride=slice(None,None,4);src=features[stride];clean=target[stride];t=times[stride]
        valid=(np.mean(clean,axis=1)>-22).astype(np.float32)
        dest=Path(output)/(uid+'.npz')
        np.savez_compressed(dest,source=src.astype(np.float16),clean=clean.astype(np.float16),
            times=t.astype(np.float32),seams=np.array(seams,np.float32),valid=valid,source_units=chosen)
        active=(proximity(t,seams)>.1)&(valid>0);delta=clean-src
        limit=4*np.log(10)/10
        return dict(id=uid,split=row['split'],path=dest.name,sha256=digest_file(dest),
            source_origins=sorted({INV.sources[u.utterance]['origin'] for u in units}),reference_sha256=record['sha256'],frames=len(t),
            controls_outside_bounds=int(np.sum(abs(delta[active])>limit)),valid_controls=int(active.sum()*24))
    except RuntimeError:
        raise
    except Exception as exc:
        # Exceptions are retained in the manifest, never replaced by a fake input.
        return dict(id=uid,split=row['split'],skipped=type(exc).__name__+': '+str(exc)[:200])


def prepare(data,bank,seed,output,workers=8):
    if output.exists():raise FileExistsError(output)
    m=json.loads((seed/'dataset.json').read_text());output.mkdir(parents=True)
    rows=m['records'];records=[];skipped=[];started=time.monotonic()
    with ProcessPoolExecutor(max_workers=workers,initializer=initialize,initargs=(str(bank),str(data))) as pool:
        for i,row in enumerate(pool.map(example,[(r,str(seed),str(output)) for r in rows],chunksize=2),1):
            (skipped if 'skipped' in row else records).append(row)
            if i%100==0:print(json.dumps(dict(processed=i,total=len(rows),accepted=len(records),skipped=len(skipped),seconds=time.monotonic()-started)),flush=True)
    m.update(records=records,skipped=skipped,seed_dataset_sha256=digest_file(seed/'dataset.json'),
        training_inputs_are_feature_warped_not_actual_psola_outputs=False,
        supervision='Actual cross-recording PSOLA plus current splice renderer, aligned to real original spectral envelopes; clean identity and known EQ examples online',
        reference_controls_outside_bounds=sum(r['controls_outside_bounds'] for r in records),
        valid_reference_controls=sum(r['valid_controls'] for r in records),
        target_origins={s:[r['id'] for r in records if r['split']==s] for s in ('train','validation')},seconds=time.monotonic()-started)
    (output/'dataset.json').write_text(json.dumps(m,ensure_ascii=False,indent=2))
    print(json.dumps(dict(complete=True,accepted=len(records),skipped=len(skipped),counts={s:len(v) for s,v in m['target_origins'].items()})),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('data','bank','seed','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--workers',type=int,default=8);prepare(**vars(p.parse_args()))
