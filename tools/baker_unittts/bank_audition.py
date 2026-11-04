"""First-stage listening: frozen-bank-conditioned controls, fixed source paths."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

import numpy as np
import soundfile as sf

from .audition import CASES
from .bank import digest_file
from .bank_controls import Predictor
from .controls_audition import duration_tier,map_time,build_page,ACCEPTED_RULES
from .dsp_controls import render_controls
from .synthesize import Inventory,pinyin_phrases,render,bounded_join


def controlled_span(inventory,span,controls,mode):
    sequence=inventory.units[span.utterance]
    first,last=sequence[span.first],sequence[span.last]
    wave,rate=inventory.wave(span.utterance);padding=round(rate*.04)
    a=max(0,first.start-padding);b=min(len(wave),last.end+padding)
    patch=wave[a:b].copy();duration=len(patch)/rate
    chosen=controls['units'][span.target_start:span.target_start+span.length]
    if len(chosen)!=span.length:raise ValueError('Incomplete span controls')
    for unit,control in zip(sequence[span.first:span.last+1],chosen):
        if (control['unit_id']!=unit.uid or control['token']!=unit.token or
                len(control['phones'])!=len(control['duration_factors']) or
                len(control['source_durations'])!=len(control['phones'])):
            raise ValueError('Controller/source identity or phone topology mismatch')
    timing=mode in ('duration','both') and any(abs(f-1)>.005 for c in chosen for f in c['duration_factors'])
    pitching=mode in ('pitch','both') and any(abs(f)>.1 for c in chosen for f in c['pitch_delta_semitones'])
    if not timing and not pitching:
        return wave[first.start:last.end].copy(),rate,dict(method='identity',mode=mode,
            utterance=span.utterance,first=span.first,last=span.last,modified=False)
    intervals=[];pitch_time=[];pitch_source=[]
    for offset,index in enumerate(range(span.first,span.last+1)):
        unit=sequence[index];control=controls['units'][span.target_start+offset]
        if control['unit_id']!=unit.uid:raise ValueError('Controller/source path mismatch')
        acoustic=inventory.acoustic_unit(unit)
        cursor=(unit.start-a)/rate
        for d,factor in zip(control['source_durations'],control['duration_factors']):
            intervals.append((cursor,cursor+d,factor));cursor+=d
        t=acoustic['frame_times'].astype(float)+(unit.start-a)/rate
        f=acoustic['frame_f0'].astype(float)
        mask=(t>=0)&(t<=duration)&(f>0)&(acoustic['frame_voiced']>0)
        pitch_time.extend(t[mask].tolist());pitch_source.extend(f[mask].tolist())
    dt,df=duration_tier(intervals,duration)
    pt=hz=None
    if pitching and len(pitch_time)>=3:
        # Source-unit analysis handles overlap in natural runs; retain one copy
        # of each true source time, never concatenate duplicate frame sequences.
        order=np.argsort(pitch_time);t=np.array(pitch_time)[order];f=np.array(pitch_source)[order]
        _,unique=np.unique(np.round(t,6),return_index=True);pt=t[unique];hz=f[unique]
        for offset,index in enumerate(range(span.first,span.last+1)):
            unit=sequence[index];control=controls['units'][span.target_start+offset]
            start=(unit.start-a)/rate;end=(unit.end-a)/rate
            mask=(pt>=start)&(pt<=end);relative=(pt[mask]-start)/(end-start)
            delta=np.interp(relative,(np.arange(24)+.5)/24,control['pitch_delta_semitones'])
            # Smooth correction back to the untouched surrounding source. This
            # is a deterministic guard, not a trained splice/noise repair model.
            weight=np.clip(np.minimum(pt[mask]-start,end-pt[mask])/.01,0,1)
            hz[mask]*=np.exp(delta*weight*np.log(2)/12)
        hz=np.clip(hz,75,600)
    out,detail=render_controls(patch,rate,pt,hz,dt if timing else None,df if timing else None)
    start=(first.start-a)/rate;end=(last.end-a)/rate
    oa=round((map_time(start,dt,df) if timing else start)*rate)
    ob=round((map_time(end,dt,df) if timing else end)*rate)
    if oa<0 or ob>len(out)+3 or ob<=oa:raise ValueError('Controlled span output crop inconsistent')
    detail.update(utterance=span.utterance,first=span.first,last=span.last,mode=mode,
                  applied_timing=timing,applied_pitch=pt is not None,
                  native_source_padding_samples=[first.start-a,b-last.end],
                  source_unit_ids=[u.uid for u in sequence[span.first:span.last+1]],
                  output_core_samples=min(ob,len(out))-oa)
    return out[oa:min(ob,len(out))].copy(),rate,detail


def phrase(inventory,spans,controls,mode):
    blocks=[];records=[]
    for span in spans:
        block,rate,detail=controlled_span(inventory,span,controls,mode)
        blocks.append(block);records.append(detail)
    result=blocks[0];joins=[]
    for previous,current,block in zip(spans,spans[1:],blocks[1:]):
        af0=inventory.units[previous.utterance][previous.last].f['f0'][-1]
        bf0=inventory.units[current.utterance][current.first].f['f0'][0]
        result,detail=bounded_join(result,block,rate,af0,bf0);joins.append(detail)
    return result,rate,dict(rendered_spans=records,joins=joins)


def audition(bank,full,planner,checkpoint,output):
    if output.exists():raise FileExistsError('Use a fresh immutable audition directory')
    inventory=Inventory(bank);reference=Inventory(full)
    predictor=Predictor(checkpoint,planner,inventory)
    output.mkdir(parents=True)
    report=dict(bank_conditioned=True,bank_id=inventory.bank_id,active_parameters=predictor.parameters,
                planner_sha256=predictor.planner.sha256,controller_sha256=predictor.sha256,
                first_four_paths_fixed=True,teacher_supervised=False,splice_trained=False,
                quality_passed=False,board_tested=False,cases=[])
    report['inference_sources_sha256']={p.name:digest_file(p) for p in Path(__file__).parent.glob('*.py')}
    modes=[('rule','新库 · 不调节'),('duration','同选段 · 只调时长'),('pitch','同选段 · 只调基频'),
           ('both','同选段 · 两者一起调'),('old_full','旧全库规则 · 另列参考')]
    for key,text,pinyin in CASES:
        folder=output/key;folder.mkdir()
        parts={mode:[] for mode,_ in modes};receipts={mode:[] for mode,_ in modes}
        characters=re.findall(r'[\u3400-\u9fff]',text);offset=0
        for tokens,pause in pinyin_phrases(pinyin):
            spans,_=inventory.select(tokens)
            selected=[u for span in spans for u in inventory.units[span.utterance][span.first:span.last+1]]
            controls=predictor(inventory,tokens,''.join(characters[offset:offset+len(tokens)]),selected)
            offset+=len(tokens)
            baseline,rate,joins=render(inventory,spans)
            parts['rule'].append(baseline);receipts['rule'].append(dict(spans=[vars(s) for s in spans],joins=joins))
            for mode in ('duration','pitch','both'):
                audio,rate,receipt=phrase(inventory,spans,controls,mode)
                parts[mode].append(audio);receipts[mode].append(dict(controls=controls,**receipt))
            old_spans,_=reference.select(tokens);old_audio,rate,old_joins=render(reference,old_spans)
            parts['old_full'].append(old_audio);receipts['old_full'].append(dict(spans=[vars(s) for s in old_spans],joins=old_joins))
            for mode in parts:parts[mode].append(np.zeros(round(pause*rate),np.int16))
        versions=[]
        for mode,label in modes:
            audio=np.concatenate(parts[mode]);path=folder/(mode+'.wav');sf.write(path,audio,rate,subtype='PCM_16')
            sha=digest_file(path)
            if mode=='old_full' and key in ACCEPTED_RULES and sha!=ACCEPTED_RULES[key]:raise ValueError('Accepted old rule baseline changed')
            (folder/(mode+'.json')).write_text(json.dumps(receipts[mode],ensure_ascii=False,indent=2),encoding='utf-8')
            versions.append(dict(mode=mode,label=label,path=str(path.relative_to(output)),sha256=sha,seconds=len(audio)/rate,
                clipped_samples=int(np.sum((audio==-32768)|(audio==32767)))))
        report['cases'].append(dict(key=key,text=text,pinyin=pinyin,versions=versions))
        print(json.dumps(dict(case=key,complete=True)),flush=True)
    (output/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    build_page(output,report);return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank',type=Path,required=True);p.add_argument('--full',type=Path,required=True)
    p.add_argument('--planner',type=Path,required=True);p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    audition(a.bank,a.full,a.planner,a.checkpoint,a.output)
