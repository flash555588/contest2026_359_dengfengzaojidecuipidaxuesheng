"""Sentence-context spectral controls around joins; no neural waveform generator.

Real recording spectral envelopes supervise phone-aligned cross-recording bank
features. These pairs do not have a sample-aligned waveform target. Separate
identity and known-EQ examples constrain needless edits. This first experiment
does not reconstruct missing coarticulation or establish perceptual quality.
"""
from __future__ import annotations

import argparse
import base64
from collections import defaultdict
from html import escape
import json
import math
from pathlib import Path
import random
import time

import numpy as np
from scipy import signal
import soundfile as sf
import torch
from torch import nn
from torch.nn.utils.rnn import pack_padded_sequence,pad_packed_sequence,pad_sequence
from torch.utils.data import DataLoader

from .bank import digest_file
from .bank_review import verify_files
from .program_tts import desired_for,track
from .controls_audition import duration_tier,map_time
from .synthesize import Inventory,Span,bounded_join

BANDS=np.geomspace(70,7900,25)
CENTERS=np.sqrt(BANDS[:-1]*BANDS[1:])
GAIN_LIMIT=4*np.log(10)/10  # log power, +/-4 dB. Targets are not clipped.


def proximity(times,seams,width=.14):
    if not len(seams):return np.zeros(len(times),np.float32)
    distance=np.min(abs(np.asarray(times)[:,None]-np.asarray(seams)[None,:]),axis=1)
    return np.where(distance<width,.5+.5*np.cos(np.pi*distance/width),0).astype(np.float32)


def inputs(features,times,seams):
    zone=proximity(times,seams)
    level=np.mean(features,axis=1)
    delta=np.r_[0.,np.diff(level)]
    return np.column_stack((np.clip((features+15)/8,-6,6),zone,
        np.clip(delta/4,-2,2),np.linspace(0,1,len(times)))).astype(np.float32)


class SeamSpectralNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.front=nn.Sequential(nn.Conv1d(27,64,5,padding=2),nn.SiLU(),
                                 nn.Conv1d(64,64,5,padding=2),nn.SiLU())
        self.context=nn.GRU(64,96,num_layers=2,batch_first=True,bidirectional=True)
        self.head=nn.Sequential(nn.Linear(192,96),nn.SiLU(),nn.Linear(96,24))
        nn.init.zeros_(self.head[-1].weight);nn.init.zeros_(self.head[-1].bias)

    def forward(self,x,lengths):
        valid=torch.arange(x.shape[1],device=x.device)[None,:]<lengths.to(x.device)[:,None]
        h=(x*valid[:,:,None]).transpose(1,2)
        for layer in self.front:h=layer(h)*valid[:,None,:]
        h=h.transpose(1,2)
        h=pack_padded_sequence(h,lengths.cpu(),batch_first=True,enforce_sorted=False)
        h,_=self.context(h)
        h,_=pad_packed_sequence(h,batch_first=True,total_length=x.shape[1])
        return torch.tanh(self.head(h))*GAIN_LIMIT*x[:,:,24:25]*valid[:,:,None]


def prepare(data,bank,output,maximum=None):
    if output.exists():raise FileExistsError(output)
    manifest=verify_files(bank);inv=Inventory(bank/'bank.sqlite')
    metadata=json.loads((data/'dataset.json').read_text())
    if digest_file(data/'dataset.json')!=manifest['control_dataset_sha256']:
        raise ValueError('Bank/reference dataset binding mismatch')
    output.mkdir(parents=True);rng=np.random.default_rng(20261029)
    acoustics={};groups=defaultdict(list);started=time.monotonic()
    for seq in inv.units.values():
        for u in seq:
            a=inv.acoustic_unit(u);acoustics[u.uid]=a
            groups[(u.token,tuple(str(p) for p in a['phones'] if p))].append(u)
    records=[];target_origins={s:[] for s in ('train','validation')};outside=total=0
    for number,r in enumerate(metadata['records']):
        if r.get('split') not in target_origins or 'skipped' in r:continue
        if maximum and len(target_origins[r['split']])>=maximum:continue
        path=data/r['path']
        if digest_file(path)!=r['sha256']:raise ValueError('Reference features changed')
        with np.load(path,allow_pickle=False) as f:d=dict(f)
        selected=[]
        for i,token in enumerate(d['tokens']):
            candidates=[u for u in groups[(str(token),tuple(str(p) for p in d['phones'][i] if p))]
                        if inv.sources[u.utterance]['origin']!=r['id']]
            if not candidates:break
            duration=float(d['durations'][i].sum())
            def cost(u):
                a=acoustics[u.uid];mask=(a['voiced']>0)&(d['voiced'][i]>0)
                pitch=float(np.mean(abs(a['log_f0'][mask]-d['log_f0'][i][mask]))) if mask.any() else .3
                contiguous=bool(selected and selected[-1].utterance==u.utterance and selected[-1].ordinal+1==u.ordinal)
                return abs(np.log(u.f['duration']/duration))+.8*pitch-.6*contiguous
            selected.append(min(candidates,key=cost))
        if len(selected)!=len(d['tokens']):continue
        # Coarse envelopes are phone-aligned, not sample-aligned. No waveform L1.
        time_mask=(d['frame_times']>=d['starts'][0])&(d['frame_times']<=d['ends'][-1])
        times=d['frame_times'][time_mask].astype(float)
        target=d['acoustic_features'][time_mask].astype(np.float32)
        source=np.full_like(target,-22.);valid=np.zeros(len(times),np.float32);seams=[]
        for i,u in enumerate(selected):
            a=acoustics[u.uid];start,end=d['starts'][i],d['ends'][i]
            mask=(times>=start)&(times<=end)
            sd=np.asarray(a['durations'],float);td=np.asarray(d['durations'][i],float)
            count=int(np.sum(sd>0));sd=sd[:count];td=td[:count]
            source_t=np.interp(times[mask]-start,np.r_[0,np.cumsum(td)],np.r_[0,np.cumsum(sd)])
            for band in range(24):source[mask,band]=np.interp(source_t,a['frame_times'],a['features'][:,band])
            valid[mask]=1
            if i and not (selected[i-1].utterance==u.utterance and selected[i-1].ordinal+1==u.ordinal):
                seams.append(float(start))
        if not seams:continue
        # Smooth the supervision over 25 ms; individual harmonics are not an EQ target.
        kernel=np.ones(5,np.float32)/5
        for band in range(24):
            target[:,band]=np.convolve(np.pad(target[:,band],(2,2),mode='edge'),kernel,'valid')
            source[:,band]=np.convolve(np.pad(source[:,band],(2,2),mode='edge'),kernel,'valid')
        stride=slice(None,None,4);t=times[stride];clean=target[stride];src=source[stride];v=valid[stride]
        zone=proximity(t,seams);delta=clean-src
        active=(zone>.1)&(v>0)
        outside+=int(np.sum(abs(delta[active])>GAIN_LIMIT));total+=int(active.sum()*24)
        # Preserve unbounded real targets. Rendering limits are explicit.
        name=r['id']+'.npz';file=output/name
        np.savez_compressed(file,source=src.astype(np.float16),clean=clean.astype(np.float16),
                            times=t.astype(np.float32),seams=np.array(seams,np.float32),valid=v,
                            source_units=np.array([u.uid for u in selected],np.int64))
        records.append(dict(id=r['id'],split=r['split'],path=name,sha256=digest_file(file),
                            reference_sha256=r['sha256'],frames=len(t),
                            source_origins=sorted({inv.sources[u.utterance]['origin'] for u in selected})))
        target_origins[r['split']].append(r['id'])
        if len(records)%250==0:print(json.dumps(dict(prepared=len(records),seconds=time.monotonic()-started)),flush=True)
    if set(target_origins['train'])&set(target_origins['validation']):raise ValueError('Target split leakage')
    if any(r['id'] in r['source_origins'] for r in records):raise ValueError('Identity leakage in retrieval pairs')
    report=dict(schema=1,bank_id=inv.bank_id,bank_files=manifest['files'],
        source_dataset_sha256=digest_file(data/'dataset.json'),records=records,
        target_origins=target_origins,teacher_supervised=False,waveform_generator=False,
        supervision='Real cross-recording phone-aligned spectral envelopes, plus online clean identity and known EQ corruptions',
        not_sample_aligned=True,training_inputs_are_feature_warped_not_actual_psola_outputs=True,
        reference_controls_outside_bounds=outside,valid_reference_controls=total,targets_clipped=False,
        quality_passed=False,seconds=time.monotonic()-started)
    (output/'dataset.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
    print(json.dumps(dict(complete=True,records=len(records),counts={k:len(v) for k,v in target_origins.items()})),flush=True)


class Examples:
    def __init__(self,directory,rows,train):
        self.rows=[];self.train=train
        for r in rows:
            p=directory/r['path']
            if digest_file(p)!=r['sha256']:raise ValueError('Seam training pair changed')
            with np.load(p,allow_pickle=False) as f:self.rows.append(dict(f))

    def __len__(self):return len(self.rows)

    def __getitem__(self,i):
        d=self.rows[i];src=d['source'].astype(np.float32);clean=d['clean'].astype(np.float32)
        kind=i%4
        if self.train:kind=int(torch.randint(4,()).item())
        if kind==0:src=clean.copy()  # Recorded natural transitions must survive unchanged.
        elif kind==1:
            # Known smooth EQ/gain discontinuities, paired with the exact recording.
            rng=np.random.default_rng(None if self.train else i)
            src=clean.copy();cuts=np.searchsorted(d['times'],d['seams'])
            for a,b in zip(np.r_[0,cuts],np.r_[cuts,len(src)]):
                curve=np.interp(np.linspace(0,1,24),[0,.33,.66,1],rng.normal(0,.45,4))
                src[a:b]+=curve
        x=inputs(src,d['times'],d['seams']);target=clean-src
        return dict(x=torch.from_numpy(x),target=torch.from_numpy(target),
                    valid=torch.from_numpy(d['valid'].astype(np.float32)),kind=kind)


def collate(rows):
    return dict(x=pad_sequence([r['x'] for r in rows],batch_first=True),
        target=pad_sequence([r['target'] for r in rows],batch_first=True),
        valid=pad_sequence([r['valid'] for r in rows],batch_first=True),
        lengths=torch.tensor([len(r['x']) for r in rows]),kind=torch.tensor([r['kind'] for r in rows]))


def objective(pred,b):
    zone=b['x'][:,:,24];weight=b['valid']*(zone>.01)
    error=nn.functional.smooth_l1_loss(pred,b['target'],reduction='none').mean(-1)
    main=(error*weight).sum()/weight.sum().clamp_min(1)
    pair=weight[:,1:]*weight[:,:-1]
    delta=nn.functional.smooth_l1_loss(torch.diff(pred,dim=1),torch.diff(b['target'],dim=1),reduction='none').mean(-1)
    dynamics=(delta*pair).sum()/pair.sum().clamp_min(1)
    identity=(b['kind']==0)[:,None]*weight
    keep=(pred.abs().mean(-1)*identity).sum()/identity.sum().clamp_min(1)
    frequency_roughness=(torch.diff(pred,dim=-1).square().mean(-1)*weight).sum()/weight.sum().clamp_min(1)
    return main+.25*dynamics+.3*keep+.03*frequency_roughness+.015*(pred.square().mean(-1)*weight).sum()/weight.sum().clamp_min(1)


def train(dataset,output,epochs=30,initial=None):
    if output.exists():raise FileExistsError(output)
    output.mkdir(parents=True);torch.manual_seed(20261029);torch.set_num_threads(4)
    m=json.loads((dataset/'dataset.json').read_text())
    data={s:Examples(dataset,[r for r in m['records'] if r['split']==s],s=='train') for s in ('train','validation')}
    if not all(len(v)>20 for v in data.values()):raise ValueError('Insufficient split-disjoint examples')
    loaders={s:DataLoader(d,batch_size=32,shuffle=s=='train',collate_fn=collate) for s,d in data.items()}
    net=SeamSpectralNet().cuda();parameters=sum(p.numel() for p in net.parameters())
    if initial:
        checkpoint=torch.load(initial,map_location='cpu',weights_only=True)
        if checkpoint['bank_id']!=m['bank_id']:raise ValueError('Warm start bank mismatch')
        net.load_state_dict(checkpoint['model'])
    if parameters+3856294>25000000:raise ValueError('Active model parameter budget exceeded')
    optim=torch.optim.AdamW(net.parameters(),lr=3e-4,weight_decay=1e-4)
    scheduler=torch.optim.lr_scheduler.ReduceLROnPlateau(optim,patience=3,factor=.5)
    history=[];best=float('inf');baseline=None;best_epoch=0;started=time.monotonic()
    for epoch in range(epochs+1):
        metrics={}
        for split,loader in loaders.items():
            if epoch==0 and split=='train':continue
            net.train(split=='train');total=count=0.;by_kind=defaultdict(list)
            with torch.set_grad_enabled(split=='train' and epoch>0):
                for b in loader:
                    b={k:(v.cuda() if k!='lengths' else v) for k,v in b.items()}
                    pred=net(b['x'],b['lengths']) if epoch else torch.zeros_like(b['target'])
                    loss=objective(pred,b)
                    if split=='train' and epoch:
                        optim.zero_grad();loss.backward();nn.utils.clip_grad_norm_(net.parameters(),1.);optim.step()
                    n=len(b['lengths']);total+=float(loss.detach())*n;count+=n
                    zone=b['valid']*(b['x'][:,:,24]>.01)
                    mae=((pred-b['target']).abs().mean(-1)*zone).sum(1)/zone.sum(1).clamp_min(1)
                    edits=(pred.abs().mean(-1)*zone).sum(1)/zone.sum(1).clamp_min(1)
                    for k,e,edit in zip(b['kind'].tolist(),mae.detach().tolist(),edits.detach().tolist()):by_kind[k].append((e,edit))
            metrics[split]=dict(loss=total/count,kinds={str(k):dict(log_power_mae=float(np.mean([a for a,b in rows])),
                mean_absolute_edit_db=float(np.mean([b for a,b in rows])*10/np.log(10))) for k,rows in by_kind.items()})
        row=dict(epoch=epoch,**metrics,seconds=time.monotonic()-started);history.append(row)
        print(json.dumps(row),flush=True)
        (output/'history.json').write_text(json.dumps(history,indent=2))
        if epoch==0:baseline=metrics['validation'];continue
        score=metrics['validation']['loss'];scheduler.step(score)
        if score<best:
            best=score;best_epoch=epoch
            torch.save(dict(model=net.state_dict(),parameters=parameters,bank_id=m['bank_id'],
                dataset_sha256=digest_file(dataset/'dataset.json'),epoch=epoch,validation=metrics['validation']),output/'best.pt')
        if epoch-best_epoch>=7:break
    report=dict(parameters=parameters,total_with_global_planner=parameters+3856294,best_epoch=best_epoch,
        baseline=baseline,validation=history[best_epoch]['validation'],epochs=len(history)-1,
        bank_id=m['bank_id'],dataset_sha256=digest_file(dataset/'dataset.json'),
        train_utterances=len(data['train']),validation_utterances=len(data['validation']),
        teacher_supervised=False,waveform_generator=False,quality_passed=False,seconds=time.monotonic()-started,
        initial_sha256=digest_file(initial) if initial else None,
        actual_psola_training_inputs=not m['training_inputs_are_feature_warped_not_actual_psola_outputs'],
        source_sha256=digest_file(Path(__file__)),
        limitation='Spectral controls cannot reconstruct missing coarticulation; paired spectral loss is not a perceptual acceptance test')
    (output/'report.json').write_text(json.dumps(report,indent=2));return report


def analyse(audio,rate):
    g=math.gcd(rate,16000);a=signal.resample_poly(audio.astype(float)/32768,16000//g,rate//g)
    if len(a)<400:raise ValueError('Insufficient phrase context')
    frames=np.lib.stride_tricks.sliding_window_view(a,400)[::80]
    power=abs(np.fft.rfft(frames*np.hanning(400),n=1024))**2
    frequency=np.fft.rfftfreq(1024,1/16000)
    f=np.stack([np.log(np.maximum(power[:,(frequency>=lo)&(frequency<hi)].mean(1),1e-12))
                for lo,hi in zip(BANDS[:-1],BANDS[1:])],1)
    return f,(np.arange(len(frames))*80+200)/16000


def filter_controls(audio,rate,times,gains):
    if gains.shape!=(len(times),24) or not np.isfinite(gains).all() or np.max(abs(gains))>GAIN_LIMIT+1e-5:
        raise ValueError('Invalid spectral controls')
    if not np.any(gains):return audio.copy(),dict(method='identity',modified=False)
    width=round(rate*.025);hop=round(rate*.005);nfft=round(rate*.064)
    pad=width;original=np.pad(audio.astype(float)/32768,(pad,pad))
    count=1+math.ceil((len(original)-width)/hop);needed=(count-1)*hop+width
    original=np.pad(original,(0,needed-len(original)))
    frames=np.lib.stride_tricks.sliding_window_view(original,width)[::hop]
    win=np.hanning(width);freq=np.fft.rfftfreq(nfft,1/rate)
    ft=(np.arange(count)*hop+width/2-pad)/rate
    frame_g=np.stack([np.interp(ft,times,gains[:,b],left=0,right=0) for b in range(24)],1)
    spectrum=np.fft.rfft(frames*win,n=nfft)
    for i in range(count):
        gain=np.interp(freq,CENTERS,frame_g[i],left=0,right=0)/2
        spectrum[i]*=np.exp(gain)
    rendered=np.fft.irfft(spectrum,n=nfft)[:,:width]*win
    out=np.zeros(len(original));weight=np.zeros(len(original))
    for i,frame in enumerate(rendered):
        a=i*hop;out[a:a+width]+=frame;weight[a:a+width]+=win*win
    out=out[pad:pad+len(audio)]/np.maximum(weight[pad:pad+len(audio)],1e-12)*32768
    if not np.isfinite(out).all() or np.max(out)>32767 or np.min(out)<-32768:
        raise ValueError('Spectral rendering would clip; no normalization applied')
    return np.rint(out).astype(np.int16),dict(method='bounded_spectral_gain',modified=True,
        maximum_gain_db=float(np.max(abs(gains))*10/np.log(10)),phase='original STFT phase',
        samples=len(audio),sample_rate=rate,pitch_control=False,duration_control=False)


def repair_clicks(audio,rate,seams,methods):
    out=audio.copy();rows=[]
    for t,method in zip(seams,methods):
        if method!='microfade':continue
        center=round(t*rate);n=round(rate*.0004);a=center-n;b=center+n
        if a<2 or b+2>=len(out):continue
        # Cubic Hermite bridge fixes value/derivative jumps without deleting time.
        left=float(out[a]);right=float(out[b]);width=b-a
        slope_left=float(out[a]-float(out[a-1]));slope_right=float(out[b+1]-float(out[b]))
        # Bound slopes so legitimate noisy onsets cannot cause an interpolation overshoot.
        slope_limit=max(abs(left),abs(right),256)/max(width,1)*2
        slope_left=np.clip(slope_left,-slope_limit,slope_limit);slope_right=np.clip(slope_right,-slope_limit,slope_limit)
        u=np.linspace(0,1,width+1)
        bridge=(2*u**3-3*u**2+1)*left+(u**3-2*u**2+u)*width*slope_left+(-2*u**3+3*u**2)*right+(u**3-u**2)*width*slope_right
        if max(bridge)>32767 or min(bridge)<-32768:
            rows.append(dict(sample=center,rejected='clipping'));continue
        out[a:b+1]=np.rint(bridge).astype(np.int16)
        rows.append(dict(sample=center,first=a,last=b,width_ms=width/rate*1000))
    return out,rows


class Controller:
    def __init__(self,path,bank_id):
        c=torch.load(path,map_location='cpu',weights_only=True)
        if c['bank_id']!=bank_id:raise ValueError('Seam model/bank identity mismatch')
        self.model=SeamSpectralNet();self.model.load_state_dict(c['model']);self.model.eval()
        self.parameters=c['parameters'];self.sha256=digest_file(path)

    @torch.inference_mode()
    def enhance(self,audio,rate,seams,strength=1.):
        features,times=analyse(audio,rate);stride=slice(None,None,4)
        for b in range(24):features[:,b]=np.convolve(np.pad(features[:,b],(2,2),mode='edge'),np.ones(5)/5,'valid')
        x=inputs(features[stride],times[stride],seams)
        gains=self.model(torch.from_numpy(x)[None],torch.tensor([len(x)]))[0].numpy()*strength
        return filter_controls(audio,rate,times[stride],gains)


def phrase_layout(inventory,store,row):
    """Recover exact cut positions analytically; never resynthesize the baseline."""
    total=0;seams=[];methods=[];plan=row['plan'];rate=None
    for i,receipt in enumerate(row['spans']):
        span=Span(**receipt['span']);seq=inventory.units[span.utterance]
        first,last=seq[span.first],seq[span.last];meta=inventory.sources[span.utterance]
        rate=meta['rate'];pad=round(.04*rate)
        a=max(0,first.start-pad);b=min(meta['frames'],last.end+pad)
        intervals=[]
        for j,u in enumerate(seq[span.first:span.last+1]):
            source,target,_,_=desired_for(store.unit(u),plan,span.target_start+j)
            cursor=(u.start-a)/rate
            for d,f in zip(source,target/source):intervals.append((cursor,cursor+d,f));cursor+=d
        dt,df=duration_tier(intervals,(b-a)/rate)
        length=round(map_time((last.end-a)/rate,dt,df)*rate)-round(map_time((first.start-a)/rate,dt,df)*rate)
        removed=0
        if i:
            join=row['joins'][i-1];removed=join['removed_samples']
            seams.append((total-removed/2)/rate);methods.append(join['method'])
        total+=length-removed
    return total,rate,seams,methods


def audition(previous,bank,checkpoint,output):
    if output.exists():raise FileExistsError(output)
    torch.set_num_threads(2);inv=Inventory(bank/'bank.sqlite');controller=Controller(checkpoint,inv.bank_id)
    from .program_tts import AcousticStore
    store=AcousticStore(inv);old=json.loads((previous/'comparison.json').read_text());output.mkdir(parents=True)
    report=dict(bank_id=inv.bank_id,model_sha256=controller.sha256,additional_parameters=controller.parameters,
        total_active_parameters=3856294+controller.parameters,quality_passed=False,cases=[],
        source_sha256={p.name:digest_file(p) for p in Path(__file__).parent.glob('*.py')})
    training=json.loads((checkpoint.parent/'report.json').read_text())
    if not training.get('actual_psola_training_inputs'):raise ValueError('Audition requires actual-renderer pair training')
    report['training']=training
    sections=[]
    for case in old['cases']:
        key=case['key'];folder=output/key;folder.mkdir();rows=json.loads((previous/key/'program_direct.json').read_text())
        originals=[];clicks=[];spectrals=[];combined=[];details=[]
        baseline,rate=sf.read(previous/key/'program_direct.wav',dtype='int16');cursor=0
        for row in rows:
            length,sr,seams,methods=phrase_layout(inv,store,row)
            if sr!=rate:raise ValueError('Mixed rates in baseline')
            result=baseline[cursor:cursor+length].copy()
            if len(result)!=length:raise ValueError('Baseline receipt duration mismatch')
            cursor+=length+(round(rate*.13) if len(originals)+1<len(rows) else 0)
            fixed,repairs=repair_clicks(result,rate,seams,methods)
            try:
                enhanced,eq=controller.enhance(result,rate,seams)
                both,eq_both=controller.enhance(fixed,rate,seams)
            except ValueError as exc:
                raise ValueError('Do not silently replace a failed enhancement: '+key) from exc
            originals.append(result);clicks.append(fixed);spectrals.append(enhanced);combined.append(both)
            details.append(dict(seams_seconds=seams,old_methods=methods,click_repairs=repairs,spectral=eq,combined=eq_both))
        if cursor!=len(baseline):raise ValueError('Saved baseline layout mismatch: '+key)
        # Copy the exact original pauses from the already accepted audition.
        baseline,rate=sf.read(previous/key/'program_direct.wav',dtype='int16')
        positions=[];cursor=0
        for audio in originals:
            # Phrase starts are exact; pause length is reported by the source audition.
            if not np.array_equal(baseline[cursor:cursor+len(audio)],audio):raise ValueError('Baseline reproduction changed: '+key)
            positions.append(cursor);cursor+=len(audio)
            if len(positions)<len(originals):
                pause=round(rate*.13)
                if np.any(baseline[cursor:cursor+pause]):raise ValueError('Unexpected baseline pause')
                cursor+=pause
        versions=[];cards=[]
        for mode,label,parts in [('baseline','0 · 原程序直调',originals),('click','1 · 只处理短接缝',clicks),
                                 ('spectral','2 · 整句模型控制频谱',spectrals),('combined','3 · 两者一起',combined)]:
            joined=baseline.copy()
            for start,audio in zip(positions,parts):joined[start:start+len(audio)]=audio
            path=folder/(mode+'.wav');sf.write(path,joined,rate,subtype='PCM_16')
            if mode=='baseline' and digest_file(path)!=digest_file(previous/key/'program_direct.wav'):raise ValueError('Baseline WAV changed')
            versions.append(dict(mode=mode,path=str(path.relative_to(output)),sha256=digest_file(path),seconds=len(joined)/rate,
                clipped_samples=int(np.sum((joined==-32768)|(joined==32767))),changed_samples=int(np.sum(joined!=baseline))))
            cards.append(f'<article><h3>{escape(label)}</h3><audio controls preload="metadata" src="data:audio/wav;base64,{base64.b64encode(path.read_bytes()).decode()}"></audio></article>')
        (folder/'controls.json').write_text(json.dumps(details,ensure_ascii=False,indent=2))
        report['cases'].append(dict(key=key,text=case['text'],versions=versions,details=details))
        sections.append('<section><h2>'+escape(case['text'])+'</h2><div class="grid">'+''.join(cards)+'</div></section>')
        print(json.dumps(dict(case=key,complete=True,joins=sum(len(r['seams_seconds']) for r in details))),flush=True)
    (output/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>固定程序直调：接缝对照</title>
<style>body{background:#121b24;color:#e5edf5;font:16px/1.7 system-ui;margin:0}main{max-width:1600px;margin:auto;padding:24px}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px}article{background:#1e2d3a;padding:15px;border-radius:9px;min-width:0}audio{width:100%;height:43px}section{border-top:1px solid #456;padding:20px 0}h3{font-size:16px}p{color:#bccbd8}@media(max-width:1000px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}</style><main><h1>固定程序直调：接缝处理对照</h1>
<p>第 0 列与上一次程序直调 WAV 哈希完全相同。所有列固定选段、音高目标和时长，未做响度归一化。</p>
<p>第 1 列只在原微淡化接缝处作 0.8ms 插值；第 2 列由整句上下文模型预测接缝附近的 24 频带增益（最多 ±4dB），沿用原 STFT 相位；第 3 列合并两种处理。模型不输出波形采样，不控制基频或时长。</p>
<p>训练参考真实标贝录音，未用教师。训练输入经过实际 PSOLA 直调及拼接，以音素对齐后的原始录音频谱包络为参考，另外加入自然语音保持原样和已知均衡扰动的样本；它不能补出素材里缺失的连读发音。本实验未通过真人音质或 P4 验收，不能据此声称拼接感已消除。</p>'''+f'<p>接缝模型 {controller.parameters:,} 参数；加全局规划器共 {controller.parameters+3856294:,} 参数。实际渲染配对：{training["train_utterances"]} 句训练、{training["validation_utterances"]} 句验证。</p>'+''.join(sections)+'''</main><script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause()})},true)</script></html>'''
    (output/'listening.html').write_text(html,encoding='utf-8');return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='op',required=True)
    a=sub.add_parser('prepare');a.add_argument('--data',type=Path,required=True);a.add_argument('--bank',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--maximum',type=int)
    a=sub.add_parser('train');a.add_argument('--dataset',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--epochs',type=int,default=30);a.add_argument('--initial',type=Path)
    a=sub.add_parser('audition');a.add_argument('--previous',type=Path,required=True);a.add_argument('--bank',type=Path,required=True);a.add_argument('--checkpoint',type=Path,required=True);a.add_argument('--output',type=Path,required=True)
    args=vars(p.parse_args());op=args.pop('op');globals()[op](**args)
