#!/usr/bin/env python3
"""Package real native LVGL frames; no browser recreation of the animation."""
from pathlib import Path
import argparse,json
import numpy as np
from PIL import Image

def frame(path,palette=True):
    values=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(600,1024).astype(np.uint32)
    rgb=np.stack([((values>>11)&31)*255//31,((values>>5)&63)*255//63,(values&31)*255//31],axis=2).astype('uint8')
    image=Image.fromarray(rgb)
    return image.quantize(colors=256) if palette else image

HTML='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Grok Ball · 动态审核</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f6f8;color:#222831;font-family:"Segoe UI","Microsoft YaHei",sans-serif}header{padding:28px 4vw;background:#fff;border-bottom:1px solid #e2e5eb}h1{font-size:28px;font-weight:600;margin:0 0 10px}p{color:#7a8190;font-size:13px;line-height:1.8;margin:0}a{color:#7764a3;text-decoration:none}nav{display:flex;gap:10px;align-items:center;margin-top:18px;flex-wrap:wrap}nav a{margin-left:auto}button{font:inherit;border:1px solid #dfe1e8;border-radius:8px;background:#fff;padding:9px 16px;cursor:pointer;color:#5e6372}.active{background:#7764a3;color:#fff;border-color:#7764a3}main{padding:28px 4vw 50px;max-width:1700px;margin:auto}h2{font-size:19px;margin:0 0 16px;font-weight:600}.effects,.states{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:20px}.states{grid-template-columns:repeat(3,minmax(0,1fr));gap:16px}.card{background:white;border:1px solid #e2e4eb;border-radius:12px;overflow:hidden}.card img{display:block;width:100%;aspect-ratio:1024/600;object-fit:contain}.label{padding:13px 16px;font-size:13px;border-top:1px solid #edf0f4}.label small{float:right;color:#9097a5;font-size:11px}section{margin-bottom:34px}.notice{margin-top:12px;font-size:12px}.lead{margin-bottom:22px}.tip{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-bottom:14px}.tip h2{margin:0}footer{font-size:12px;color:#8d929d;border-top:1px solid #e2e4eb;padding:24px 4vw}@media(max-width:900px){.states{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:640px){.effects,.states{grid-template-columns:1fr}h1{font-size:24px}}
</style><header><h1>Grok Ball · 动态审核</h1><p>32 种表情已接入现有 OuO 入口。恢复原版浅色球体、黑色眼睛与渐变彩带；前后彩带按球面深度分层。全部图片与动图来自真实 QuickJS / LVGL 离线渲染。</p><nav><button class="active" data-theme="light">浅色主题</button><button data-theme="dark">深色主题</button><a href="index.html">全部界面 ↗</a></nav></header><main>
<section><h2>连续动作</h2><div class="effects" id="effects"></div><p class="notice">连续帧可以检查彩带绕到球面前后时的遮挡。动图循环播放，点击查看原尺寸。</p></section>
<section><h2>表情入口与 AI 状态</h2><p class="lead">实际应用新增“表情”入口：按生命周期、情绪反应、工作状态选择。面板打开时暂停背景动画，关闭后恢复。</p><div class="effects" id="entries"></div></section>
<section><div class="tip"><h2>全部 32 种表情</h2><button id="motion">播放每张表情动效</button></div><p class="lead">点击图片查看原图。浅色、深色的每张表情均有 3.2 秒动画预览，包含原引擎眨眼、眼环轮换和表情动作。</p><div class="states" id="states"></div></section></main><footer>源项目 <a href="https://github.com/tycoding/grok-ball" target="_blank" rel="noopener">tycoding/grok-ball</a> · MIT。RGB565 色深和渐变插值会与浏览器略有差异。已完成离线验证，未刷机，实板性能尚未验证。</footer><script>
const ROWS=__ROWS__;let theme='light',motion=false;const escape=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function card(file,title,info){return '<article class="card"><a href="'+file+'" target="_blank"><img src="'+file+'" alt="'+escape(title)+'" loading="lazy"></a><div class="label">'+escape(title)+'<small>'+info+'</small></div></article>'}
function render(){document.querySelector('#effects').innerHTML=[['orbit','思考 · 前后环绕','6 秒'],['spin','自旋 · 彩带拖尾','6 秒'],['sleep','睡眠 · 漂浮 zzz','6 秒'],['confetti','任务完成 · 撒花庆祝','4 秒'],['gaze','注视 · 连续跟随','4 秒'],['tour','全部表情 · 连续切换','32 种']].map(e=>card(theme+'-grok-'+e[0]+'.gif',e[1],e[2]+' / 循环')).join('');document.querySelector('#entries').innerHTML=[['picker-life','生命周期'],['picker-emotion','情绪反应'],['picker-agent','工作状态'],['ai-tips','AI 状态与文字提示']].map(e=>card(theme+'-ouo-'+e[0]+'.png',e[1],'实际应用')).join('');document.querySelector('#states').innerHTML=ROWS.map(r=>card(motion?theme+'-grok-'+r.id.slice(-2)+'.gif':theme+'-'+r.id+'.png',r.title.replace('Grok Ball · ',''),'ID '+r.id.slice(-2))).join('');document.querySelector('#motion').textContent=motion?'显示静态截图':'播放每张表情动效'}
document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;document.querySelectorAll('[data-theme]').forEach(x=>x.classList.toggle('active',x===b));render()});document.querySelector('#motion').onclick=()=>{motion=!motion;render()};render();</script></html>'''

def main():
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);args=parser.parse_args()
    out=args.directory;raw=out/'grok-frames';rows=json.loads((out/'screens.json').read_text(encoding='utf-8'))
    states=[r for r in rows if r['id'].startswith('ouo-state-') and r['theme']=='浅色'];assert len(states)==32
    animations=[]
    for theme in ['light','dark']:
        tour=[]
        for row in states:
            code=row['id'][-2:];files=sorted(raw.glob(f'{theme}-grok-{code}-*.rgb565'));assert len(files)==16,code
            frames=[frame(p) for p in files];name=f'{theme}-grok-{code}.gif'
            frames[0].save(out/name,save_all=True,append_images=frames[1:],duration=200,loop=0,disposal=2)
            tour.extend(frames[i].copy() for i in [0,2,4,8,12,15])
            with Image.open(out/name) as encoded:count=encoded.n_frames
            animations.append(dict(file=name,frames=count,native_frames=16,duration_ms=3200,id=row['id']))
        name=f'{theme}-grok-tour.gif'
        tour[0].save(out/name,save_all=True,append_images=tour[1:],duration=200,loop=0,disposal=2)
        with Image.open(out/name) as encoded:count=encoded.n_frames
        animations.append(dict(file=name,frames=count,native_frames=len(tour),duration_ms=len(tour)*200))
        for effect,native_count in [('orbit',60),('spin',60),('sleep',60),('confetti',40),('gaze',40)]:
            files=sorted(raw.glob(f'{theme}-grok-{effect}-*.rgb565'));assert len(files)==native_count
            frames=[frame(p) for p in files];name=f'{theme}-grok-{effect}.gif'
            frames[0].save(out/name,save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
            with Image.open(out/name) as encoded:count=encoded.n_frames
            animations.append(dict(file=name,frames=count,native_frames=native_count,duration_ms=native_count*100))
            # Pick an actual frame with a clear front ribbon for the cover.
            if effect=='orbit':
                def front_pixels(im):
                    pixels=np.array(im.convert('RGB'))[220:360,410:615].astype('int16')
                    return np.count_nonzero(pixels.max(axis=2)-pixels.min(axis=2)>45)
                index=max(range(len(frames)),key=lambda i:front_pixels(frames[i]))
                frame(files[index],False).save(out/f'{theme}-ouo-thinking.png')
    (out/'grok.html').write_text(HTML.replace('__ROWS__',json.dumps(states,ensure_ascii=False)),encoding='utf-8')
    (out/'grok-animations.json').write_text(json.dumps({'renderer':'native QuickJS / LVGL RGB565 frames','resolution':[1024,600],'flashed':False,'animations':animations},ensure_ascii=False,indent=2),encoding='utf-8')
    print('GROK_REVIEW_OK',len(animations),'GIFs / 32 states')

if __name__=='__main__':main()
