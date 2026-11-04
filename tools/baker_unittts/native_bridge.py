"""Diagnostic real-transition grafts from train recordings, preserving a cached baseline.

This is a source-library experiment, not learned seam repair. Extra recordings
are reported separately from the frozen bank. No missing transition is invented.
"""
from __future__ import annotations

import argparse
import base64
from collections import defaultdict
from html import escape
import json
from pathlib import Path

import numpy as np
import soundfile as sf

from .bank import digest_file
from .program_tts import AcousticStore,ProgramCosts,desired_for,render_phrase,render_span
from .seam_spectral import phrase_layout
from .synthesize import Inventory,Span


def correlation(a,b):
    a=a-a.mean();b=b-b.mean()
    return float(np.dot(a,b)/max(np.linalg.norm(a)*np.linalg.norm(b),1e-10))


def align_patch(baseline,bridge,center,a,b,rate):
    """Phase-align endpoints, then blend inside a fixed, time-preserving window."""
    n=b-a;blend=min(round(.018*rate),n//4)
    if blend<8:raise ValueError('Insufficient stable blend context')
    positions=np.arange(n,dtype=float)+center-(a+b)/2
    limit=round(rate*.002)
    if positions[0]-limit<0 or positions[-1]+limit>=len(bridge):raise ValueError('Bridge analysis handles exhausted')
    before=baseline[a:b].astype(float)
    offsets=[];scores=[]
    for region in (slice(0,blend),slice(n-blend,n)):
        best=(-float('inf'),0)
        for lag in range(-limit,limit+1,4):
            source=np.interp(positions[region]+lag,np.arange(len(bridge)),bridge)
            score=correlation(before[region],source)-.02*abs(lag)/max(limit,1)
            if score>best[0]:best=(score,lag)
        scores.append(best[0]);offsets.append(best[1])
    # A local monotonic map reconciles phase at both ends. Its actual duration
    # and induced pitch perturbation are reported; the sentence length is fixed.
    query=positions+np.linspace(offsets[0],offsets[1],n)
    warped=np.interp(query,np.arange(len(bridge)),bridge)
    db=[]
    for region in (slice(0,blend),slice(n-blend,n)):
        ratio=np.sqrt((np.mean(before[region]**2)+1)/(np.mean(warped[region]**2)+1))
        db.append(float(np.clip(20*np.log10(ratio),-3,3)))
    warped*=10**(np.linspace(db[0],db[1],n)/20)
    weights=np.ones(n)
    ramp=.5-.5*np.cos(np.linspace(0,np.pi,blend))
    weights[:blend]=ramp;weights[-blend:]=ramp[::-1]
    result=before*(1-weights)+warped*weights
    if not np.isfinite(result).all() or result.max()>32767 or result.min()<-32768:raise ValueError('Bridge would clip')
    detail=dict(first=a,last=b-1,blend_samples=blend,endpoint_correlations=scores,phase_offsets_samples=offsets,
        endpoint_gain_db=db,local_time_map_slope=float(1+(offsets[1]-offsets[0])/max(n-1,1)))
    return np.rint(result).astype(np.int16),detail


class Library:
    def __init__(self,inventory,data):
        self.inv=inventory;self.store=AcousticStore(inventory,data);self.pairs=defaultdict(list);self.used={}
        for seq in inventory.units.values():
            for a,b in zip(seq,seq[1:]):
                if 0<=b.start-a.end<=round(inventory.sources[a.utterance]['rate']*.02):
                    self.pairs[(a.token,b.token)].append((a,b))

    def candidates(self,plan,index):
        sub={k:plan[k][index-1:index+1] for k in ('tokens','phones','phone_durations','f0','voiced')}
        choices=[]
        for left,right in self.pairs[(sub['tokens'][0],sub['tokens'][1])]:
            score=0.;valid=True
            for i,u in enumerate((left,right)):
                try:
                    a=self.store.unit(u);sd,td,goal,voiced=desired_for(a,sub,i)
                    mask=(a['voiced']>0)&(voiced>=.4);ratio=td/sd
                    if mask.sum()<3 or ratio.min()<.5 or ratio.max()>2 or np.max(abs(goal[mask]-a['log_f0'][mask]))>np.log(2):
                        valid=False;break
                    score+=float(np.mean(abs(goal[mask]-a['log_f0'][mask]))*12/np.log(2)+2*np.mean(abs(np.log(ratio))))
                except ValueError:valid=False;break
            if valid:choices.append((score,left,right,sub))
        return sorted(choices,key=lambda r:r[0])[:8]

    def apply(self,baseline,rate,row,width=.07):
        length,sr,seams,_=phrase_layout(self.base_inventory,self.base_store,row)
        if length!=len(baseline) or sr!=rate:raise ValueError('Cached phrase layout mismatch')
        result=baseline.copy();receipts=[];bounds=np.r_[0.,seams,len(baseline)/rate]
        indices=[s['span']['target_start'] for s in row['spans'][1:]]
        for number,(seam,index) in enumerate(zip(seams,indices)):
            # Keep modifications inside the adjacent phones and away from other seams.
            left_phone=float([v for v in row['plan']['phone_durations'][index-1] if v>0][-1])
            right_phone=float(row['plan']['phone_durations'][index][0])
            tail=min(width,left_phone*.65,(seam-bounds[number])*.42)
            head=min(width,max(.025,right_phone*.85),(bounds[number+2]-seam)*.42)
            a=round((seam-tail)*rate);b=round((seam+head)*rate)
            token_pair=row['plan']['tokens'][index-1:index+1]
            best=None;rejected=[];available=len(self.pairs[tuple(token_pair)])
            for cost,left,right,plan in self.candidates(row['plan'],index):
                try:
                    audio,_,positions,receipt=render_span(self.inv,Span(left.utterance,left.ordinal,right.ordinal,0),self.store,plan,refinements=0)
                    center=(positions[0]['output_end']+positions[1]['output_start'])/2
                    patch,alignment=align_patch(baseline,audio,center,a,b,rate)
                    # Match stable ends, rather than minimizing a click-only score.
                    score=.05*cost+sum(1-c for c in alignment['endpoint_correlations'])
                    if best is None or score<best[0]:best=(score,patch,left,right,alignment,receipt)
                except ValueError as exc:rejected.append(str(exc))
            if best is None:
                receipts.append(dict(tokens=token_pair,target_index=index,available=available,applied=False,rejected=rejected));continue
            score,patch,left,right,alignment,receipt=best
            result[a:b]=patch
            origin=self.inv.sources[left.utterance].get('origin',left.utterance)
            receipts.append(dict(tokens=token_pair,target_index=index,available=available,applied=True,
                source_origin=origin,source_units=[left.uid,right.uid],source_span=[left.ordinal,right.ordinal],
                alignment=alignment,source_render=receipt,score=score,rejected=rejected))
            self.used[(left.utterance,left.ordinal)]=(left,right)
        return result,receipts

    def export_sources(self,output):
        output.mkdir();rows=[]
        for i,(left,right) in enumerate(self.used.values()):
            wave,rate=self.inv.wave(left.utterance);pad=round(rate*.04)
            a=max(0,left.start-pad);b=min(len(wave),right.end+pad)
            path=output/f'{i:04d}.wav';sf.write(path,wave[a:b],rate,subtype='PCM_16')
            rows.append(dict(path=path.name,sha256=digest_file(path),origin=left.utterance,
                original_wave_sha256=self.inv.sources[left.utterance]['sha256'],original_start_sample=a,
                source_units=[left.uid,right.uid],tokens=[left.token,right.token],rate=rate,bytes=path.stat().st_size))
        (output/'sources.json').write_text(json.dumps(rows,indent=2));return rows


def audition(previous,bank,full,data,output):
    if output.exists():raise FileExistsError(output)
    output.mkdir(parents=True);base=Inventory(bank/'bank.sqlite');base_store=AcousticStore(base)
    original=Inventory(full);library=Library(original,data)
    library.base_inventory=base;library.base_store=base_store
    old=json.loads((previous/'comparison.json').read_text());sections=[];cases=[]
    for case in old['cases']:
        folder=output/case['key'];folder.mkdir();baseline,rate=sf.read(previous/case['key']/'program_direct.wav',dtype='int16')
        rows=json.loads((previous/case['key']/'program_direct.json').read_text())
        parts={m:[] for m in ('baseline','bridge_short','bridge_wide','full_direct')};reports=[];cursor=0
        for r in rows:
            n,sr,seams,_=phrase_layout(base,base_store,r);audio=baseline[cursor:cursor+n].copy()
            parts['baseline'].append(audio)
            short,rs=library.apply(audio,rate,r,.045);wide,rw=library.apply(audio,rate,r,.08)
            parts['bridge_short'].append(short);parts['bridge_wide'].append(wide)
            # This independent full-bank arm changes source selection and is explicitly labelled.
            selector=ProgramCosts(original,library.store,r['plan'])
            spans,selected=selector.select(r['plan']['tokens'])
            direct,_,detail=render_phrase(original,spans,library.store,r['plan'])
            parts['full_direct'].append(direct)
            reports.append(dict(short=rs,wide=rw,full_direct=dict(selected=selected,render=detail)))
            cursor+=n
            if len(reports)<len(rows):
                pause=baseline[cursor:cursor+round(rate*.13)]
                if np.any(pause):raise ValueError('Baseline pause mismatch')
                for m in parts:parts[m].append(pause.copy())
                cursor+=len(pause)
        if cursor!=len(baseline):raise ValueError('Cached baseline size mismatch')
        cards=[];versions=[]
        for mode,label in [('baseline','0 · 原程序直调'),('bridge_short','1 · 真人连读过渡，窄窗口'),
                           ('bridge_wide','2 · 真人连读过渡，宽窗口'),('full_direct','3 · 完整训练库重新选段直调')]:
            audio=np.concatenate(parts[mode]);path=folder/(mode+'.wav');sf.write(path,audio,rate,subtype='PCM_16')
            if mode=='baseline' and digest_file(path)!=digest_file(previous/case['key']/'program_direct.wav'):raise ValueError('Baseline changed')
            if mode!='full_direct' and len(audio)!=len(baseline):raise ValueError('Transition graft changed sentence duration')
            versions.append(dict(mode=mode,path=str(path.relative_to(output)),sha256=digest_file(path),seconds=len(audio)/rate,
                clipped_samples=int(np.sum((audio==-32768)|(audio==32767)))))
            cards.append(f'<article><h3>{escape(label)}</h3><audio controls preload="metadata" src="data:audio/wav;base64,{base64.b64encode(path.read_bytes()).decode()}"></audio></article>')
        (folder/'bridges.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2))
        coverage={mode:sum(j['applied'] for r in reports for j in r[mode]) for mode in ('short','wide')}
        joins=sum(len(r['wide']) for r in reports)
        cases.append(dict(key=case['key'],text=case['text'],joins=joins,applied=coverage,versions=versions))
        sections.append('<section><h2>'+escape(case['text'])+f'</h2><p>窄/宽窗口覆盖 {coverage["short"]}/{coverage["wide"]} 个接点，共 {joins} 个。</p><div class="grid">'+''.join(cards)+'</div></section>')
        print(json.dumps(dict(case=case['key'],joins=joins,applied=coverage)),flush=True)
    sources=library.export_sources(output/'extra-sources')
    report=dict(cases=cases,active_parameters=3856294,neural_seam_model=False,extra_sources=len(sources),
        extra_source_wav_bytes=sum(r['bytes'] for r in sources),bank_id=base.bank_id,
        full_training_index_sha256=digest_file(full),quality_passed=False,
        source_sha256={p.name:digest_file(p) for p in Path(__file__).parent.glob('*.py')})
    (output/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>真人连读过渡实验</title><style>body{background:#121b24;color:#e5edf5;font:16px/1.7 system-ui;margin:0}main{max-width:1700px;margin:auto;padding:24px}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px}article{background:#1e2d3a;padding:15px;border-radius:9px;min-width:0}audio{width:100%;height:43px}section{border-top:1px solid #456;padding:20px 0}h3{font-size:16px}p{color:#bccbd8}@media(max-width:1000px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}</style><main><h1>真人连读过渡实验</h1>
<p>第 0 列与之前程序直调 WAV 完全一致。第 1、2 列保留这版选段和整句长度，调用完整训练库中真实连续说出的相同带调音节对，局部直调后替换接点附近；接点两端相位对齐并淡化。局部相位对齐会有少量时间/音高扰动，记录在回执中。没有合适素材的接点保持原样。</p>
<p>第 3 列独立使用完整训练库重新选段及直调，会改变原选段和时长，仅作素材覆盖对照。本页未使用上一版频谱增益模型，也未训练新模型；全局规划器仍为 3,856,294 参数。</p>
<p>本实验额外读取约 3.70GB 的完整训练音频，不能据此认为原 333MB 库独立支持相同效果。extra-sources/ 仅打包这六句最终选用的原始过渡素材，不代表通用过渡库的完整大小。没有响度归一化，未通过真人听感或 P4 验收。</p>'''+''.join(sections)+'''</main><script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause()})},true)</script></html>'''
    (output/'listening.html').write_text(html,encoding='utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('previous','bank','full','data','output'):p.add_argument('--'+name,type=Path,required=True)
    audition(**vars(p.parse_args()))
