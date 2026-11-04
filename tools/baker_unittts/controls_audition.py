"""Listen to the first control stage with the accepted rule paths held fixed."""
from __future__ import annotations

import argparse
import base64
import hashlib
from html import escape
import json
from pathlib import Path
import re
import zipfile

import numpy as np
import soundfile as sf

from .audition import CASES
from .controls import Predictor
from .dsp_controls import render_controls
from .synthesize import Inventory,pinyin_phrases,render,bounded_join

INITIALS={'b','p','m','f','d','t','n','l','g','k','h','j','q','x','zh','ch','sh','r','z','c','s'}
ACCEPTED_RULES={'welcome':'bc43d926e944ab6f6962b75129657922913d03161bd2142d052fca434987840a',
                'walk':'e8229b2e8332bab839fc19ed1ce6d3f63435b32dd569afc42a5af9079295b7fd'}


def duration_tier(intervals,duration):
    points={0.:1.,duration:1.}
    previous=0.
    for start,end,factor in intervals:
        eps=min(.0002,(end-start)/10)
        if start-previous>.0005:
            points[min(duration,previous+.0001)]=1.
            points[max(0.,start-.0001)]=1.
        points[start+eps]=float(factor);points[end-eps]=float(factor)
        previous=end
    if previous<duration-.0001:points[previous+.0001]=1.
    ordered=sorted(points.items())
    return np.array([x for x,_ in ordered]),np.array([y for _,y in ordered])


def map_time(value,times,factors):
    knots=np.unique(np.r_[0.,times[(times>0)&(times<value)],value])
    return float(np.trapezoid(np.interp(knots,times,factors),knots))


def controlled_span(inventory,span,features,plan,mode):
    sequence=inventory.units[span.utterance]
    first,last=sequence[span.first],sequence[span.last]
    wave,rate=inventory.wave(span.utterance)
    pad=round(rate*.04);a=max(0,first.start-pad);b=min(len(wave),last.end+pad)
    patch=wave[a:b].copy();duration=len(patch)/rate
    intervals=[];limited=0;phone_records=[]
    for offset,index in enumerate(range(span.first,span.last+1)):
        unit=sequence[index];q=span.target_start+offset
        durations=features['durations'][index];labels=[str(x) for x in features['phones'][index] if x]
        target=np.array(plan['phone_durations'][q]);desired_labels=plan['phones'][q]
        target_total=float(target.sum())
        initial_time=sum(float(d) for p,d in zip(labels,durations) if p in INITIALS)
        final_time=sum(float(d) for p,d in zip(labels,durations) if p not in INITIALS)
        cursor=unit.start/rate-a/rate
        for slot,(label,source_d) in enumerate(zip(labels,durations)):
            if label in INITIALS:
                ratio=(target[slot]/source_d) if labels==desired_labels else 1.
                factor=float(np.clip(ratio,.95,1.05))
            else:
                ratio=(target[slot]/source_d) if labels==desired_labels else (target_total-initial_time)/max(final_time,.001)
                factor=float(np.clip(ratio,.85,1.18))
            limited+=abs(float(ratio)-factor)>.0001
            intervals.append((cursor,cursor+float(source_d),factor));cursor+=float(source_d)
            phone_records.append(dict(token=unit.token,phone=label,source_seconds=float(source_d),
                                      requested_ratio=float(ratio),applied_ratio=factor))
    dt,df=duration_tier(intervals,duration)
    pitch_times=pitch_hz=None
    if mode in ('pitch','both'):
        mask=((features['frame_times']>=a/rate)&(features['frame_times']<=b/rate)&
              (features['frame_f0']>0)&(features['frame_voiced']>0))
        absolute=features['frame_times'][mask].astype(float)
        original=features['frame_f0'][mask].astype(float)
        if len(absolute)>=3:
            desired=original.copy()
            for offset,index in enumerate(range(span.first,span.last+1)):
                unit=sequence[index];q=span.target_start+offset
                select=(absolute>=unit.start/rate)&(absolute<=unit.end/rate)
                relative=(absolute[select]-unit.start/rate)/((unit.end-unit.start)/rate)
                knots=(np.arange(24)+.5)/24
                target=np.exp(np.interp(relative,knots,np.log(np.clip(plan['f0'][q],75,600))))
                confidence=np.interp(relative,knots,plan['voiced'][q])
                edge=np.minimum(relative,1-relative)*((unit.end-unit.start)/rate)/.01
                weight=np.clip(edge,0,1)*(confidence>=.4)
                desired[select]=original[select]*np.exp(weight*np.log(target/original[select]))
            pitch_times=absolute-a/rate;pitch_hz=np.clip(desired,75,600)
    timing=mode in ('duration','both')
    output,detail=render_controls(patch,rate,pitch_times,pitch_hz,
                                  dt if timing else None,df if timing else None)
    start=(first.start-a)/rate;end=(last.end-a)/rate
    oa=round((map_time(start,dt,df) if timing else start)*rate)
    ob=round((map_time(end,dt,df) if timing else end)*rate)
    if oa<0 or ob>len(output)+3 or ob<=oa:
        raise ValueError('PSOLA output crop/time map inconsistent')
    detail.update(utterance=span.utterance,first=span.first,last=span.last,
                  mode=mode,duration_limited_phones=limited if timing else 0,
                  phone_controls=phone_records if timing else [],
                  source_padding_samples=[first.start-a,b-last.end],
                  output_core_samples=min(ob,len(output))-oa)
    return output[oa:min(ob,len(output))].copy(),rate,detail


def controlled_phrase(inventory,spans,features,plan,mode):
    blocks=[];details=[]
    for span in spans:
        block,rate,detail=controlled_span(inventory,span,features[span.utterance],plan,mode)
        blocks.append(block);details.append(detail)
    result=blocks[0];joins=[]
    for previous,current,block in zip(spans,spans[1:],blocks[1:]):
        af0=inventory.units[previous.utterance][previous.last].f['f0'][-1]
        bf0=inventory.units[current.utterance][current.first].f['f0'][0]
        result,detail=bounded_join(result,block,rate,af0,bf0);joins.append(detail)
    return result,rate,dict(spans=details,joins=joins)


def build_page(output,report):
    sections=[]
    for case in report['cases']:
        cards=[]
        for version in case['versions']:
            audio=(output/version['path']).read_bytes()
            if hashlib.sha256(audio).hexdigest()!=version['sha256']:raise ValueError('Audition changed')
            cards.append(f'''<article><h3>{escape(version['label'])}</h3><audio controls preload="metadata"
 src="data:audio/wav;base64,{base64.b64encode(audio).decode()}"></audio>
<p>{version['seconds']:.3f} 秒 · 48kHz</p></article>''')
        sections.append(f'<section><h2>{escape(case["text"])}</h2><div class="grid">'+''.join(cards)+'</div></section>')
    intro=('前四列固定同一组新库规则选段：不调节、只调时长、只调基频、一起调整。第五列是之前的旧全库规则，素材不同，单独判断。'
           if report.get('bank_conditioned') else
           '前四列固定同一组旧全库规则选段：原基线、只调时长、只调基频、一起调整。第五列仅试听新库规则，素材不同，单独判断。')
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>第一阶段：时长与基频控制试听</title><style>
body{background:#121b24;color:#e4edf4;font:16px/1.7 system-ui;margin:0}main{max-width:1600px;margin:auto;padding:24px}
h1{font-size:26px}h2{font-size:21px}h3{font-size:16px}p{color:#b7c7d5}.grid{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:12px}
article{background:#1c2c39;border-radius:10px;padding:14px;min-width:0}article:last-child{border:1px dashed #a3b0b9}audio{width:100%;height:42px}section{padding:18px 0;border-top:1px solid #405362}
@media(max-width:1100px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:550px){.grid{grid-template-columns:1fr}}
</style><main><h1>第一阶段：时长与基频控制</h1>
<p>'''+escape(intro)+'''</p>
<p>训练监督来自标贝真实录音，没有教师蒸馏；模型合计 '''+f"{report['active_parameters']:,}"+''' 参数，不输出波形。保留原始 48kHz，不做响度归一化。</p>
<p>新接缝模型尚未训练。前四列沿用相同的原拼接算法，窗口可能随处理后的波形变化。为限制失真，基频修正最多 ±2 半音，声母时长系数 0.95–1.05、韵母 0.85–1.18；超出部分受限，不代表已准确实现全部目标。</p>
'''+''.join(sections)+'''<p>这是第一阶段诊断试听，未通过音质、声调正确性或 P4 实板验收。播放一段会暂停其他音频。</p>
</main><script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause()})},true)</script></html>'''
    (output/'listening.html').write_text(html,encoding='utf-8')


def audition(database,data,checkpoint,new_bank,output):
    if output.exists():raise FileExistsError('Auditions are immutable; choose a fresh directory')
    output.mkdir(parents=True)
    inventory=Inventory(database);new_inventory=Inventory(new_bank)
    predictor=Predictor(checkpoint);feature_cache={}
    report=dict(model_sha256=predictor.sha256,active_parameters=predictor.parameters,
                teacher_supervised=False,first_four_paths_fixed=True,
                new_splice_model=False,quality_passed=False,board_tested=False,cases=[])
    names=[('rule','旧全库 · 规则基线'),('duration','同选段 · 只调时长'),
           ('pitch','同选段 · 只调基频'),('both','同选段 · 两者一起调'),('new_bank','新库 · 规则（另列）')]
    for key,text,pinyin in CASES:
        case=output/key;case.mkdir()
        parts={mode:[] for mode,_ in names};receipts={mode:[] for mode,_ in names}
        offset=0;chars=re.findall(r'[\u3400-\u9fff]',text)
        for tokens,pause in pinyin_phrases(pinyin):
            spans,_=inventory.select(tokens)
            plan=predictor(tokens,''.join(chars[offset:offset+len(tokens)]));offset+=len(tokens)
            for span in spans:
                if span.utterance not in feature_cache:
                    with np.load(data/(span.utterance+'.npz'),allow_pickle=False) as features:
                        feature_cache[span.utterance]=dict(features)
            baseline,rate,joins=render(inventory,spans)
            parts['rule'].append(baseline)
            receipts['rule'].append(dict(spans=[vars(s) for s in spans],joins=joins))
            for mode in ('duration','pitch','both'):
                result,rate,receipt=controlled_phrase(inventory,spans,feature_cache,plan,mode)
                parts[mode].append(result);receipts[mode].append(dict(plan=plan,**receipt))
            new_spans,_=new_inventory.select(tokens)
            new_audio,rate,new_joins=render(new_inventory,new_spans)
            parts['new_bank'].append(new_audio)
            receipts['new_bank'].append(dict(spans=[vars(s) for s in new_spans],joins=new_joins))
            for mode in parts:parts[mode].append(np.zeros(round(pause*rate),np.int16))
        versions=[]
        for mode,label in names:
            audio=np.concatenate(parts[mode]);path=case/(mode+'.wav')
            sf.write(path,audio,rate,subtype='PCM_16')
            digest=hashlib.sha256(path.read_bytes()).hexdigest()
            if mode=='rule' and key in ACCEPTED_RULES and digest!=ACCEPTED_RULES[key]:
                raise ValueError('Accepted full-bank rule baseline changed')
            (case/(mode+'.json')).write_text(json.dumps(receipts[mode],ensure_ascii=False,indent=2),encoding='utf-8')
            versions.append(dict(mode=mode,label=label,path=str(path.relative_to(output)),sha256=digest,
                                 seconds=len(audio)/rate,clipped_samples=int(np.sum((audio==-32768)|(audio==32767)))))
        report['cases'].append(dict(key=key,text=text,pinyin=pinyin,versions=versions))
        print(json.dumps(dict(case=key,complete=True)),flush=True)
    (output/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    build_page(output,report)
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--database',type=Path,required=True);p.add_argument('--data',type=Path,required=True)
    p.add_argument('--checkpoint',type=Path,required=True);p.add_argument('--new-bank',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();audition(a.database,a.data,a.checkpoint,a.new_bank,a.output)
