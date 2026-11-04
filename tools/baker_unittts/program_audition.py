"""Audition programmatic source selection and direct global-target rendering."""
from __future__ import annotations

import argparse
import base64
from html import escape
import json
from pathlib import Path
import re

import numpy as np
import soundfile as sf

from .audition import CASES
from .bank import digest_file
from .controls import Predictor
from .controls_audition import ACCEPTED_RULES
from .program_tts import AcousticStore,ProgramCosts,render_phrase,raw_phrase
from .synthesize import Inventory,pinyin_phrases,render


def page(output,report):
    sections=[]
    for case in report['cases']:
        cards=[]
        for v in case['versions']:
            audio=(output/v['path']).read_bytes()
            if digest_file(output/v['path'])!=v['sha256']:raise ValueError('Audition WAV changed')
            note='独立全库，仅作参考' if v['mode']=='full_rule' else ''
            cards.append(f'<article><h3>{escape(v["label"])}</h3><audio controls preload="metadata" src="data:audio/wav;base64,{base64.b64encode(audio).decode()}"></audio><p>{v["seconds"]:.3f} 秒 · 48kHz</p><small>{note}</small></article>')
        rows=[]
        for r in case['goal_checks']:
            value='无法可靠测量' if r['mean_f0_error_st'] is None else f'{r["mean_f0_error_st"]:.2f} 半音'
            rows.append(f'<tr><td>{escape(r["character"])}</td><td>{escape(r["token"])}</td><td>{value}</td><td>{1000*(r["actual_duration_seconds"]-r["target_duration_seconds"]):+.2f} ms</td><td>{"达到数值门槛" if r["target_tracking_pass"] else "仍有偏差"}</td></tr>')
        sections.append(f'<section><h2>{escape(case["text"])}</h2><div class="grid">'+''.join(cards)+'</div><details><summary>程序直调后的逐字目标核对</summary><p>这是与规划器目标的一致性，不能代替声调听感验收。</p><table><thead><tr><th>字</th><th>音节</th><th>基频平均偏差</th><th>时长偏差</th><th>状态</th></tr></thead><tbody>'+''.join(rows)+'</tbody></table></details></section>')
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>程序选段与直接调到全局目标</title><style>
body{margin:0;background:#121b24;color:#e5edf5;font:16px/1.7 system-ui}main{max-width:1600px;margin:auto;padding:24px}h1{font-size:26px}h2{font-size:21px}h3{font-size:16px}
p,small{color:#b9c9d6}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px}article{padding:15px;background:#1e2d3a;border-radius:9px;min-width:0}article:first-child{border:1px dashed #7f949f}audio{width:100%;height:43px}section{padding:20px 0;border-top:1px solid #3f5263}details{margin-top:15px}table{border-collapse:collapse;width:100%;text-align:left}td,th{padding:7px;border-bottom:1px solid #3f5263}
@media(max-width:1000px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:550px){.grid{grid-template-columns:1fr}main{padding:15px}}
</style><main><h1>程序选段，直接调到全局目标</h1>
<p>只有全局规划器使用神经网络（3,856,294 参数）；选段与声学调节全部由程序执行，未加载旧神经调节器。</p>
<p>0：你之前的旧全库规则参考；1：同一份 333MB 新库旧规则；2：程序按目标时长、完整基频轮廓和连续性选段；3：与 2 固定相同选段，直接调到全局目标。</p>
<p>没有教师蒸馏或波形生成，没有响度归一化。程序不再静默截断到 ±2 半音，声明的渲染范围为 ±12 半音、时长系数 0.5–2.0，范围外拒绝该素材。声学处理后重新测量实际基频和时长；较大调节的听感仍需单独判断。</p>
<p>全局规划目标本身可能有误。接缝仍沿用旧规则，本页不表示已经解决拼接感。点击“逐字目标核对”可查看仍未达到目标的字。</p>
'''+''.join(sections)+'''<p>音频全部内嵌，可离线播放。播放一段会暂停其他音频。尚未通过真人音质、语义声调或 P4 实板验收。</p>
</main><script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause()})},true)</script></html>'''
    (output/'listening.html').write_text(html,encoding='utf-8')


def audition(bank,full,planner,output):
    if output.exists():raise FileExistsError('Use a fresh audition directory')
    inventory=Inventory(bank);reference=Inventory(full);store=AcousticStore(inventory)
    predictor=Predictor(planner);output.mkdir(parents=True)
    report=dict(bank_id=inventory.bank_id,planner_sha256=predictor.sha256,active_parameters=predictor.parameters,
                neural_adjuster=False,learned_selector=False,teacher_supervised=False,quality_passed=False,cases=[])
    report['source_sha256']={p.name:digest_file(p) for p in Path(__file__).parent.glob('*.py')}
    modes=[('full_rule','0 · 旧全库规则参考'),('bank_rule','1 · 新库旧规则'),
           ('program_select','2 · 程序选段，不调节'),('program_direct','3 · 同选段，程序直调')]
    for key,text,pinyin in CASES:
        folder=output/key;folder.mkdir();chars=re.findall(r'[\u3400-\u9fff]',text);offset=0
        parts={m:[] for m,_ in modes};receipts={m:[] for m,_ in modes};goal_checks=[]
        for tokens,pause in pinyin_phrases(pinyin):
            phrase_text=''.join(chars[offset:offset+len(tokens)])
            plan=predictor(tokens,phrase_text)
            provider=ProgramCosts(inventory,store,plan);spans,selected=provider.select(tokens)
            direct,rate,details=render_phrase(inventory,spans,store,plan)
            selected_audio,_,selected_details=raw_phrase(inventory,spans,plan)
            parts['program_direct'].append(direct);parts['program_select'].append(selected_audio)
            receipts['program_direct'].append(dict(plan=plan,selected_units=selected,**details))
            receipts['program_select'].append(dict(spans=[vars(s) for s in spans],selected_units=selected,**selected_details))
            for r in details['units_after_join']:
                goal_checks.append(dict(character=chars[offset+r['target_index']],**r))
            for mode,inv in [('full_rule',reference),('bank_rule',inventory)]:
                baseline,_=inv.select(tokens);audio,rate,joins=render(inv,baseline)
                parts[mode].append(audio);receipts[mode].append(dict(spans=[vars(s) for s in baseline],joins=joins))
            offset+=len(tokens)
            for mode in parts:parts[mode].append(np.zeros(round(pause*rate),np.int16))
        versions=[]
        for mode,label in modes:
            path=folder/(mode+'.wav');audio=np.concatenate(parts[mode]);sf.write(path,audio,rate,subtype='PCM_16')
            sha=digest_file(path)
            if mode=='full_rule' and key in ACCEPTED_RULES and sha!=ACCEPTED_RULES[key]:raise ValueError('Accepted full rule reference changed')
            (folder/(mode+'.json')).write_text(json.dumps(receipts[mode],ensure_ascii=False,indent=2),encoding='utf-8')
            versions.append(dict(mode=mode,label=label,path=str(path.relative_to(output)),sha256=sha,seconds=len(audio)/rate,
                                 clipped_samples=int(np.sum((audio==-32768)|(audio==32767)))))
        report['cases'].append(dict(key=key,text=text,pinyin=pinyin,versions=versions,goal_checks=goal_checks))
        print(json.dumps(dict(case=key,complete=True,goal_pass=sum(r['target_tracking_pass'] for r in goal_checks),units=len(goal_checks))),flush=True)
    (output/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    page(output,report);return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank',type=Path,required=True);p.add_argument('--full',type=Path,required=True)
    p.add_argument('--planner',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();audition(a.bank,a.full,a.planner,a.output)
