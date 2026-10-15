#!/usr/bin/env python3
"""Publish the selected native A home-card and its common engine expressions."""
from pathlib import Path
import argparse,hashlib,json,shutil
import numpy as np
from PIL import Image

def rgb565(path,shape):
    raw=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(shape).astype(np.uint32)
    return Image.fromarray(np.stack([((raw>>11)&31)*255//31,((raw>>5)&63)*255//63,(raw&31)*255//31],axis=2).astype('uint8'))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];source=root/'output/home-ball-expressions';dest=a.output/'home-ball-expressions';dest.mkdir(exist_ok=True)
    scene_model=json.loads((source/'scenes.json').read_text(encoding='utf-8'));report=json.loads((a.native/'VALIDATION.json').read_text(encoding='utf-8'))
    assert report['prototype_only'] and report['native_frames']==960 and report['capture_format']=='card565-and-full-frame-012'
    assert report['ball_diameter']==96 and report['ball_center']==[76,65] and report['nominal_hidden_bottom_px']==24
    assert all(not report[k] for k in ['flashed','production_applied','firmware_compiled','board_tested'])
    assert report['scene_sha256']==hashlib.sha256((source/'scenes.json').read_bytes()).hexdigest()
    assert report['production_home_sha256']==hashlib.sha256((root/'project/app/espdl-quickapp/overlay/apps/system/desktop/glass_home_ui.inc').read_bytes()).hexdigest()
    assert 'HOME_BALL_NATIVE_OK' in (a.native/'native.log').read_text()
    vendor=root/'project/ouo/vendor/grok-ball.js';app=root/'output/grok-styles/mosaic/app.js'
    assert vendor.read_text(encoding='utf-8') in app.read_text(encoding='utf-8')
    for entry in scene_model['scenes']:
        assert len(entry['frames'])==30 and len(entry['textFrames'])==30
        # Waking automatically settles into idle, as it does in the source engine.
        allowed={entry['emotion'],'02'} if entry['emotion']=='01' else {entry['emotion']}
        assert set(entry['observed'])<=allowed
        for theme in ['light','dark']:
            key=entry['key'];frames=[]
            for index in range(30):
                im=rgb565(a.native/f'{theme}-{key}-{index:03}.card565',(178,285))
                if index==12:im.save(dest/f'{theme}-{key}-card.png')
                frames.append(im.quantize(colors=256,dither=Image.Dither.NONE))
            frames[0].save(dest/f'{theme}-{key}-card.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
            rgb565(a.native/f'{theme}-{key}-012.rgb565',(600,1024)).save(dest/f'{theme}-{key}.png')
    report.update(selected_variant='a',selected_by_user=True,engine_vendor_sha256=hashlib.sha256(vendor.read_bytes()).hexdigest(),scene_adapter_sha256=hashlib.sha256(app.read_bytes()).hexdigest(),sleep_letters=True,
        limits='Native LVGL design preview with vector replay; not yet integrated with the firmware or live AI events. GIFs sample at 100 ms, not device FPS.')
    (dest/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');shutil.copy2(a.native/'native.log',dest/'native.log')
    (a.output/'home-ball-expressions.html').write_text(PAGE.replace('__SCENES__',json.dumps(report['scenes'],ensure_ascii=False)),encoding='utf-8')
    options_path=root/'output/home-ball-options/OPTIONS.json';options=json.loads(options_path.read_text(encoding='utf-8'));options.update(selected_by_user='a',recommended='a');options_path.write_text(json.dumps(options,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('HOME_A_EXPRESSIONS_REVIEW_OK: selected A; 16 pinned expressions; 32 native card animations; 960 native frames; not flashed')

PAGE='''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>A 版首页 · 常见表情</title><style>
*{box-sizing:border-box}body{margin:0;background:#f6f5f8;color:#282634;font:15px/1.65 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1120px;margin:32px auto;padding:0 24px}a{color:#846796;text-decoration:none}h1{font-size:30px;font-weight:600;margin:10px 0}h2{font-size:20px;font-weight:600;margin:0}p{color:#8b8291;margin:10px 0}.badge{display:inline-block;margin-top:22px;padding:5px 12px;border-radius:20px;background:#ece6f3;color:#84699b;font-size:12px}.toolbar{display:flex;gap:12px;flex-wrap:wrap;margin:24px 0;align-items:center}.group{display:flex;gap:4px;background:#ece9f0;padding:4px;border-radius:10px}button,select{font:inherit;cursor:pointer;color:#817587}button{border:0;border-radius:8px;background:transparent;padding:7px 14px}button.active{background:white;color:#655271;box-shadow:0 1px 4px #32244312}select{padding:10px 14px;border:1px solid #e4ddea;border-radius:10px;background:white}.home-section{background:white;padding:20px;border-radius:20px;border:1px solid #e6e0eb}.home-section header{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:14px}.home{display:block;width:100%;max-width:1024px;margin:auto;border-radius:12px}.hint{font-size:12px}.gallery-heading{margin:30px 0 16px}.gallery{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}.expression{background:white;border:1px solid #e4dfea;border-radius:16px;overflow:hidden}.expression.selected{border-color:#ae8fc4;box-shadow:0 0 0 2px #ae8fc421}.expression button{width:100%;text-align:left;padding:14px 16px;border-radius:0;display:flex;justify-content:space-between;color:#594f62;font-weight:500}.selected .check{visibility:visible}.check{font-size:12px;color:#9e7cb8;visibility:hidden}.stage{padding:16px 8px;background:#f9f7fb;display:flex;justify-content:center}.card{display:block;width:285px;max-width:100%;height:auto;image-rendering:pixelated}.dark .stage{background:#23232b}.footer{font-size:12px;padding:26px 0;color:#a096a6}@media(max-width:960px){.gallery{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:650px){main{padding:0 12px;margin:22px auto}h1{font-size:24px}.gallery{grid-template-columns:1fr}.home-section{padding:12px}.home-section header{align-items:flex-start;flex-direction:column}}
</style></head><body><main><a href="home-ball-options.html">← 四版对比</a><br><span class="badge">已选 A · 96 px · 底部遮挡 1/4</span><h1>A 版定稿布局，补齐常见表情</h1><p>小球固定靠左，上方留白；暖白纯色、卡片内遮挡和下方对话入口保持一致。表情沿用独立小球的原始定义。</p><div class="toolbar"><div class="group"><button class="active" data-theme="light">浅色</button><button data-theme="dark">深色</button></div><select id="scene" aria-label="选择表情"></select><div class="group"><button class="active" data-motion="true">卡片动画</button><button data-motion="false">静态画面</button></div></div><section class="home-section"><header><h2>放回首页</h2><span id="current-label"></span></header><a id="full-link" target="_blank"><img class="home" id="home" alt="A 版所选表情的原生首页"></a><p class="hint">首页显示当前表情的截图；下方可查看卡片内动画。点击首页打开原图。</p></section><div class="gallery-heading"><h2>16 种常见状态</h2><p class="hint">点击表情名称，即可切换上方首页预览。</p></div><div class="gallery" id="gallery"></div><p class="footer">两种主题共 960 帧原生 LVGL 渲染，检查卡片遮挡、睡眠字母与对话入口。当前为设计审核预览，尚未接入固件和实时 AI 状态，未烧录。<a href="home-ball-expressions/validation.json">验证记录 ↗</a></p></main><script>
const scenes=__SCENES__;let theme='light',selected='idle',motion=true;const chooser=document.querySelector('#scene'),gallery=document.querySelector('#gallery');
chooser.innerHTML=scenes.map(s=>`<option value="${s.key}">${s.name}</option>`).join('');gallery.innerHTML=scenes.map(s=>`<article class="expression" data-key="${s.key}"><button aria-label="查看${s.name}">${s.name}<span class="check">当前</span></button><div class="stage"><img class="card" alt="${s.name}卡片表情"></div></article>`).join('');
function render(){document.body.classList.toggle('dark',theme==='dark');chooser.value=selected;const item=scenes.find(s=>s.key===selected);document.querySelector('#current-label').textContent=item.name;const url=`home-ball-expressions/${theme}-${selected}.png`;document.querySelector('#home').src=url;document.querySelector('#full-link').href=url;document.querySelectorAll('.expression').forEach(el=>{el.classList.toggle('selected',el.dataset.key===selected);el.querySelector('button').setAttribute('aria-pressed',el.dataset.key===selected);el.querySelector('img').src=`home-ball-expressions/${theme}-${el.dataset.key}-card.${motion?'gif':'png'}`});document.querySelectorAll('[data-theme]').forEach(b=>b.classList.toggle('active',b.dataset.theme===theme));document.querySelectorAll('[data-motion]').forEach(b=>b.classList.toggle('active',(b.dataset.motion==='true')===motion));}
document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;render()});document.querySelectorAll('[data-motion]').forEach(b=>b.onclick=()=>{motion=b.dataset.motion==='true';render()});chooser.onchange=()=>{selected=chooser.value;render()};gallery.addEventListener('click',e=>{const button=e.target.closest('button');if(button){selected=button.closest('article').dataset.key;render()}});render();
</script></body></html>'''

if __name__=='__main__':main()
