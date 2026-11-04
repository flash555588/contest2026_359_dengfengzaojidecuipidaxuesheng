"""Period-aware cut search on jointly controlled spans with original handles.

Selected source units and global targets stay fixed. Both the old and new join
arms consume the same one-time rendered blocks; PSOLA repeat variation therefore
cannot masquerade as a splice improvement. No waveform network, normalization,
or independently applied pitch edits are used here.
"""
from __future__ import annotations

import argparse
from html import escape
import json
from pathlib import Path
import shutil

import numpy as np
import soundfile as sf

from .bank import digest_file
from .program_tts import AcousticStore,inspect_output,render_span,track
from .synthesize import Inventory,Span,bounded_join

UNVOICED_ONSETS={'b','p','d','t','g','k','j','q','zh','ch','z','c','f','s','sh','x','h'}


def edge_pitch(context,anchor,rate):
    a=max(0,anchor-round(.045*rate));b=min(len(context),anchor+round(.045*rate))
    t,hz,strength=track(context[a:b],rate)
    if not len(t):return 0.
    near=(abs(t-(anchor-a)/rate)<.022)&(hz>0)&(strength>=.65)
    return float(np.median(hz[near])) if np.sum(near)>=2 else 0.


def correlation(x,y):
    x=x.astype(float)-np.mean(x);y=y.astype(float)-np.mean(y)
    return float(np.dot(x,y)/max(np.linalg.norm(x)*np.linalg.norm(y),1e-9))


def choose_join(left,left_end,right,right_start,rate,left_f0,right_f0,protect_onset=False):
    """Return cut anchors and a convex overlap from verified analysis handles.

    Voiced edges search a half-period on each side. Relative timing movement is
    <=2.5ms, no interpolation/time warp changes the pitch. Unvoiced/transient
    edges use zero cut movement and a short 1.5ms total overlap. The overlap
    consumes both original handles, so it does not introduce a silent valley.
    """
    if left.dtype!=np.int16 or right.dtype!=np.int16:raise ValueError('PCM16 required')
    voiced=left_f0>0 and right_f0>0 and not protect_onset
    if voiced:
        period=rate/max(left_f0,right_f0)
        half=min(round(.0035*rate),max(12,round(period*.5)))
        search=min(round(period*.5),round(.003*rate))
        step=max(1,round(rate*.000125))
        shifts=sorted(set([0,*range(-search,search+1,step)]))
    else:
        half=max(2,round(.00075*rate));shifts=[0]
    candidates=[]
    for dl in shifts:
        la=left_end+dl-half;lb=left_end+dl+half
        if la<0 or lb>len(left):continue
        x=left[la:lb].astype(float)
        for dr in shifts:
            if abs(dl-dr)>round(.0025*rate):continue
            ra=right_start+dr-half;rb=right_start+dr+half
            if ra<0 or rb>len(right):continue
            y=right[ra:rb].astype(float)
            corr=correlation(x,y)
            rms_x=np.sqrt(np.mean(x*x)+1);rms_y=np.sqrt(np.mean(y*y)+1)
            db=float(abs(20*np.log10(rms_x/rms_y)))
            score=(1-corr)+.035*min(db,18)+.08*(abs(dl)+abs(dr))/max(rate*.003,1)
            candidates.append((score,abs(dl)+abs(dr),dl,dr,corr,db,x,y))
    if not candidates:raise ValueError('Missing rendered analysis handles for cycle join')
    _,_,dl,dr,corr,db,x,y=min(candidates,key=lambda v:v[:2])
    ramp=.5-.5*np.cos(np.linspace(0,np.pi,2*half))
    mixed=np.rint(x*(1-ramp)+y*ramp).astype(np.int16)
    return mixed,dict(method='cycle_aligned_handles' if voiced else 'onset_protected_handles',
        half_window_samples=half,left_cut_shift=dl,right_cut_shift=dr,
        timing_change_samples=dl-dr,correlation=corr,edge_level_difference_db=db,
        left_f0=left_f0,right_f0=right_f0,protect_onset=protect_onset,
        waveform_generator=False,pitch_warp=False)


def join_blocks(blocks,rate,plan,cycle):
    result=blocks[0]['core'].copy();positions=[dict(p) for p in blocks[0]['positions']]
    joins=[]
    for previous,current in zip(blocks,blocks[1:]):
        before=len(result)
        left=previous['context'];right=current['context']
        index=current['span'].target_start
        if cycle:
            lf=edge_pitch(left['audio'],left['end'],rate)
            rf=edge_pitch(right['audio'],right['start'],rate)
            protected=plan['phones'][index][0] in UNVOICED_ONSETS
            bridge,detail=choose_join(left['audio'],left['end'],right['audio'],right['start'],rate,lf,rf,protected)
            h=detail['half_window_samples'];dl=detail['left_cut_shift'];dr=detail['right_cut_shift']
            cut=before+dl-h
            if cut<positions[-1]['output_start']:raise ValueError('Join would erase an earlier unit')
            # Positive left shift emits a small part of its original handle.
            if cut>before:
                result=np.r_[result,left['audio'][left['end']:left['end']+cut-before]]
            result=np.r_[result[:cut],bridge,right['audio'][right['start']+dr+h:right['end']]]
            offset=before+dl-dr
            positions[-1]['output_end']=before+dl
            detail.update(left_original_handle_samples=max(0,dl+h),right_original_handle_samples=max(0,h-dr),
                          output_seam_sample=before+dl,removed_samples=dr-dl)
        else:
            # Reproduce the original renderer's F0 analysis as well as its join.
            _,af,_=track(result[-min(len(result),round(rate*.08)):],rate)
            _,bf,_=track(current['core'][:min(len(current['core']),round(rate*.08))],rate)
            lf=float(np.median(af[af>0])) if np.any(af>0) else 0.
            rf=float(np.median(bf[bf>0])) if np.any(bf>0) else 0.
            result,detail=bounded_join(result,current['core'],rate,lf,rf)
            offset=before-detail['removed_samples']
            detail['output_seam_sample']=offset
        for number,p in enumerate(current['positions']):
            start=before+dl if cycle and number==0 else p['output_start']+offset
            positions.append({**p,'output_start':start,'output_end':p['output_end']+offset})
        joins.append(detail)
    if np.any(np.diff([p['output_start'] for p in positions])<=0):raise ValueError('Non-monotone output unit positions')
    return result,dict(joins=joins,positions=positions,units_after_join=inspect_output(result,rate,positions,plan))


def audition(previous,bank,output,native=None):
    if output.exists():raise FileExistsError(output)
    output.mkdir(parents=True)
    inv=Inventory(bank/'bank.sqlite');store=AcousticStore(inv)
    old=json.loads((previous/'comparison.json').read_text())
    cases=[];sections=[]
    for case in old['cases']:
        folder=output/case['key'];folder.mkdir()
        saved,rate=sf.read(previous/case['key']/'program_direct.wav',dtype='int16')
        rows=json.loads((previous/case['key']/'program_direct.json').read_text())
        parts={mode:[] for mode in ('old_join','cycle_join')};records=[]
        for ri,row in enumerate(rows):
            blocks=[]
            for item in row['spans']:
                span=Span(**item['span'])
                core,sr,positions,detail,context=render_span(inv,span,store,row['plan'],return_context=True)
                if sr!=rate:raise ValueError('Unexpected sample rate')
                blocks.append(dict(core=core,positions=positions,context=context,span=span,render=detail))
            renders={}
            for mode in parts:
                audio,detail=join_blocks(blocks,rate,row['plan'],mode=='cycle_join')
                parts[mode].append(audio)
                if ri+1<len(rows):parts[mode].append(np.zeros(round(rate*.13),np.int16))
                renders[mode]=detail
            records.append(dict(plan=row['plan'],span_renders=[b['render'] for b in blocks],versions=renders))
        shutil.copyfile(previous/case['key']/'program_direct.wav',folder/'baseline.wav')
        variants=[('baseline','0 · 原来听过的程序直调')]
        for mode,label in [('old_join','1 · 同批渲染，原拼法'),('cycle_join','2 · 同批渲染，周期对齐拼接')]:
            sf.write(folder/(mode+'.wav'),np.concatenate(parts[mode]),rate,subtype='PCM_16');variants.append((mode,label))
        if native is not None:
            shutil.copyfile(native/case['key']/'bridge_wide.wav',folder/'native_bridge.wav')
            variants.append(('native_bridge','3 · 原版加真人连续过渡（额外素材）'))
        versions=[];cards=[]
        for mode,label in variants:
            p=folder/(mode+'.wav');audio,sr=sf.read(p,dtype='int16')
            full=int(np.sum((audio==-32768)|(audio==32767)))
            if full:raise ValueError('Full-scale samples in output '+str(p))
            versions.append(dict(mode=mode,path=str(p.relative_to(output)),sha256=digest_file(p),samples=len(audio),rate=sr,seconds=len(audio)/sr,full_scale_samples=full))
            cards.append(f'<article><h3>{escape(label)}</h3><audio controls preload="metadata" src="{case["key"]}/{mode}.wav"></audio><p>{len(audio)/sr:.3f} 秒</p></article>')
        (folder/'joins.json').write_text(json.dumps(records,ensure_ascii=False,indent=2))
        cases.append(dict(key=case['key'],text=case['text'],versions=versions))
        sections.append(f'<section><h2>{escape(case["text"])}</h2><div class="grid">'+''.join(cards)+'</div></section>')
        print(json.dumps(dict(case=case['key'],versions=versions)),flush=True)
    report=dict(cases=cases,active_parameters=3856294,bank_id=inv.bank_id,new_trainable_parameters=0,
        same_rendered_blocks_for_old_and_cycle=True,quality_passed=False,board_tested=False,
        source_sha256={p.name:digest_file(p) for p in Path(__file__).parent.glob('*.py')})
    if native is not None:
        reference=json.loads((native/'comparison.json').read_text())
        report['native_transition_reference']={k:reference[k] for k in ('extra_sources','extra_source_wav_bytes','full_training_index_sha256')}
    (output/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>程序直调：拼接处理对照</title><style>body{background:#121c25;color:#e8eff5;font:16px/1.7 system-ui;margin:0}main{max-width:1800px;margin:auto;padding:25px}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px}article{background:#1e2e3c;padding:16px;border-radius:10px;min-width:0}audio{width:100%}h3{font-size:16px}p{color:#bdcbd8}section{border-top:1px solid #456;padding:18px 0}@media(max-width:1000px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}</style><main><h1>程序直调：拼接处理对照</h1><p>第 0 列保留之前试听 WAV。第 1、2 列严格使用相同选段、同一全局目标、同一批 PSOLA 渲染结果；只更换拼法。第 2 列利用原录音两侧分析余量，对有声接点按周期搜索切点并重叠；清辅音和爆破音起始保护不移动。单接点相对时长变化不超过 2.5ms，不做频谱滤波、归一化或额外调音。拼接窗口最多延伸原切点约 6.5ms，可能包含原上下文的极短过渡，需试听检查。</p><p>第 3 列在原第 0 列上移植完整训练库中真实连续的相同带调音节对。它会使用原 333MB 库之外的额外素材；缺少可用音对时保持原样。第 3 列不是第 2 列再加一步，两者是独立实验。</p><p>全局规划器 3,856,294 参数；本轮没有新增神经模型。数字校验不代表拼接感或声调已经通过，尚未移植或验证 ESP32-P4。</p>'''+''.join(sections)+'''</main><script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause()})},true)</script></html>'''
    (output/'listening.html').write_text(html,encoding='utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('previous','bank','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--native',type=Path)
    audition(**vars(p.parse_args()))
