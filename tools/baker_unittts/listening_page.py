"""Create a standalone offline listening comparison; audio bytes stay unchanged."""
from __future__ import annotations

import argparse
import base64
import hashlib
from html import escape
from html.parser import HTMLParser
import json
from pathlib import Path
import wave


def build(folder:Path,destination:Path):
    report=json.loads((folder/'comparison.json').read_text(encoding='utf-8'))
    sections=[]
    total=0
    for row in report['cases']:
        cards=[]
        previous_hash=None
        versions=[dict(label='教师参考',path=row['teacher'],parameters=None)]+row['stages']
        for index,version in enumerate(versions):
            path=folder/version['path']
            audio=path.read_bytes()
            digest=hashlib.sha256(audio).hexdigest()
            if version.get('sha256') and digest!=version['sha256']:
                raise ValueError('Audio digest mismatch')
            with wave.open(str(path)) as w:
                duration=w.getnframes()/w.getframerate()
                rate=w.getframerate()
                if w.getsampwidth()!=2 or w.getnchannels()!=1:raise ValueError('Unexpected audition format')
            same=index>1 and digest==previous_hash
            note='与上一阶段音频完全一致' if same else ('最后两阶段选段相同，只改变接缝处理' if index==4 else '')
            label=('参考' if index==0 else str(index-1))+' · '+version['label']
            parameters='离线完整教师' if version['parameters'] is None else f"{version['parameters']:,} 参数"
            cards.append(f'''<div class="version"><h3>{escape(label)}</h3>
<audio controls preload="none" aria-label="{escape(row['text']+' '+version['label'])}" src="data:audio/wav;base64,{base64.b64encode(audio).decode()}"></audio>
<div class="meta">{duration:.3f} 秒 · {rate//1000}kHz · {parameters}</div><div class="note">{escape(note)}</div></div>''')
            previous_hash=digest
            total+=1
        sections.append(f'<section id="{row["key"]}"><h2>{escape(row["text"])}</h2><div class="versions">'+''.join(cards)+'</div></section>')
    navigation=''.join(f'<a href="#{r["key"]}">{escape(r["text"][:8])}</a>' for r in report['cases'])
    document='''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Baker TTS 累加对照试听</title>
<style>
:root{color-scheme:light dark;--bg:#f5f7fa;--fg:#192b3b;--panel:#fff;--line:#d6dde5;--muted:#526170;--accent:#086b8a}
@media(prefers-color-scheme:dark){:root{--bg:#111a23;--fg:#e4ecf4;--panel:#1b2834;--line:#374958;--muted:#b4c4d1;--accent:#7dd3e8}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.6 system-ui,sans-serif}
main{max-width:1600px;margin:auto;padding:28px 22px 50px}h1{font-size:26px;line-height:1.3;margin:0 0 12px}h2{font-size:20px;margin:0 0 14px}h3{font-size:15px;margin:0 0 12px}
p{max-width:1100px;margin:8px 0;color:var(--muted)}nav{display:flex;flex-wrap:wrap;gap:10px 20px;padding:16px 0 10px}a{color:var(--accent)}
section{padding:24px 0;border-top:1px solid var(--line);scroll-margin-top:16px}.versions{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:12px}
.version{padding:16px 12px;background:var(--panel);border-radius:8px;min-width:0}audio{display:block;width:100%;height:42px}.meta{font-size:12px;color:var(--muted);margin-top:10px}.note{font-size:12px;color:var(--accent);min-height:36px;margin-top:6px}
@media(max-width:1150px){.versions{grid-template-columns:repeat(3,minmax(0,1fr))}}@media(max-width:700px){.versions{grid-template-columns:repeat(2,minmax(0,1fr))}main{padding:20px 12px}}
@media(max-width:440px){.versions{grid-template-columns:1fr}}
</style></head><body><main><h1>逐个加入模型，听哪里发生变化</h1>
<p>参考：完整教师。0：纯规则；1：加韵律规划器；2：再加选段模型；3：再加接缝控制模型。</p>
<p>六句 × 五版本，共 30 段。四个实验阶段共用同一份约 173MB 冻结素材库；阶段 2 和 3 锁定相同选段。没有响度归一化。教师参考为 24kHz，其余保留 48kHz 原始录音采样率。</p>
<p>本轮素材库与前一次全库试听不同，请在本轮内部比较。三模型合计 1,436,463 参数；参考评分训练不等于真人音质验收。</p><nav>'''+navigation+'</nav>'+''.join(sections)+'''
<p>播放任一音频会自动暂停其他音频。所有音频已嵌入本文件，无需联网。录音来源：标贝 BZNSYP，限非商业使用。尚未进行 P4 实板验收。</p>
</main><script>
document.addEventListener('play',function(event){if(event.target.tagName!=='AUDIO')return;document.querySelectorAll('audio').forEach(function(audio){if(audio!==event.target)audio.pause();});},true);
</script></body></html>'''
    class Check(HTMLParser):
        def __init__(self):super().__init__();self.audio=0
        def handle_starttag(self,tag,attrs):
            if tag=='audio':
                self.audio+=1
                src=dict(attrs).get('src','')
                if not src.startswith('data:audio/wav;base64,'):raise ValueError('External audio reference')
    parser=Check();parser.feed(document)
    if parser.audio!=total or total!=30:raise ValueError('Missing comparison players')
    destination.write_text(document,encoding='utf-8')
    print(json.dumps(dict(players=total,bytes=destination.stat().st_size,path=str(destination.resolve()))))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--folder',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();build(a.folder,a.output)
