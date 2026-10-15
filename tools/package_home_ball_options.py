#!/usr/bin/env python3
"""Package four native LVGL home-ball layout alternatives for review."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / 'output/home-ball-options'
    options = json.loads((source / 'OPTIONS.json').read_text(encoding='utf-8'))
    home_hash = hashlib.sha256((root / 'project/app/espdl-quickapp/overlay/apps/system/desktop/glass_home_ui.inc').read_bytes()).hexdigest()
    dest = args.output / 'home-ball-options'
    dest.mkdir(parents=True, exist_ok=True)
    reports = []
    for option in options['variants']:
        key = option['id']
        native = args.native / key
        report = json.loads((native / 'VALIDATION.json').read_text())
        assert report['prototype_only'] and report['native_frames'] == 120
        assert all(not report[field] for field in ['flashed', 'production_applied', 'firmware_compiled', 'board_tested'])
        assert report['production_home_sha256'] == home_hash
        assert report['scene_sha256'] == hashlib.sha256((source / key / 'scenes.json').read_bytes()).hexdigest()
        assert report['ball_diameter'] == option['diameter']
        assert report['ball_center'][0] == option['centerX']
        assert abs(report['nominal_hidden_bottom_px'] - option['diameter'] * option['occlusion']) < .001
        assert 'HOME_BALL_NATIVE_OK' in (native / 'native.log').read_text()
        folder = dest / key
        folder.mkdir(exist_ok=True)
        for theme in ['light', 'dark']:
            for scene in ['idle', 'thinking']:
                frames = []
                for path in sorted(native.glob(f'{theme}-{scene}-*.rgb565')):
                    raw = np.frombuffer(path.read_bytes(), dtype='<u2').reshape(600, 1024).astype(np.uint32)
                    rgb = np.stack([((raw >> 11) & 31) * 255 // 31, ((raw >> 5) & 63) * 255 // 63, (raw & 31) * 255 // 31], axis=2).astype('uint8')
                    image = Image.fromarray(rgb)
                    card = image.crop((108, 374, 393, 552))
                    if path.stem.endswith('-012'):
                        image.save(folder / f'{theme}-{scene}.png')
                        card.save(folder / f'{theme}-{scene}-card.png')
                    frames.append(card.quantize(colors=256, dither=Image.Dither.NONE))
                assert len(frames) == 30
                frames[0].save(folder / f'{theme}-{scene}-card.gif', save_all=True, append_images=frames[1:], duration=100, loop=0, disposal=2)
        shutil.copy2(native / 'native.log', folder / 'native.log')
        (folder / 'validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        reports.append(report)
    options['validation'] = {'variants': 4, 'native_frames': sum(r['native_frames'] for r in reports), 'themes': ['light', 'dark'], 'scenes': ['idle', 'thinking'], 'flashed': False, 'production_applied': False,
        'limits': 'Native LVGL layout prototype with vector scene replay. No firmware runtime integration or board test. GIF interval is review sampling, not device FPS.'}
    (dest / 'options.json').write_text(json.dumps(options, ensure_ascii=False, indent=2), encoding='utf-8')
    document = PAGE.replace('__OPTIONS__', json.dumps(options['variants'], ensure_ascii=False)).replace('__SELECTED__',options.get('selected_by_user','b'))
    if options.get('selected_by_user')=='a':
        document=document.replace('<div style="margin-top:24px"','<p><a href="home-ball-expressions.html">已选 A · 查看 16 种常见表情 ↗</a></p><div style="margin-top:24px"',1)
    (args.output / 'home-ball-options.html').write_text(document, encoding='utf-8')
    print('HOME_BALL_OPTIONS_OK: 4 variants, 16 full home images, 16 card animations; 480 native frames; unflashed')


PAGE = '''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>首页小球 · 四版布局选择</title><style>
*{box-sizing:border-box}body{margin:0;background:#f6f5f8;color:#282634;font:15px/1.6 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1280px;margin:36px auto;padding:0 24px}a{color:#716185;text-decoration:none}h1{font-size:32px;font-weight:600;letter-spacing:-.5px;margin:12px 0}h2{font-size:21px;font-weight:600;margin:0}p{color:#817d8d;margin:8px 0}.eyebrow{font-size:12px;letter-spacing:2px;color:#8b7d9c}.badge{display:inline-block;padding:5px 11px;background:#ebe6f1;color:#78648e;border-radius:16px;font-size:12px}.intro{max-width:760px}.toolbar{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin:26px 0 22px}.group{display:flex;gap:4px;background:#ebe9ef;border-radius:11px;padding:4px}button,select{font:inherit;cursor:pointer;color:#787181}button{background:transparent;border:0;border-radius:8px;padding:7px 13px}button.active{background:white;color:#4d405e;box-shadow:0 1px 4px #32244312}select{background:white;border:1px solid #e5e0e9;border-radius:10px;padding:9px 12px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.option{background:#fff;border:1px solid #e6e1ea;border-radius:19px;overflow:hidden;transition:border-color .2s,box-shadow .2s}.option.selected{border-color:#aa91c3;box-shadow:0 0 0 2px #aa91c31f}.option header{padding:18px 20px;display:flex;align-items:center;gap:12px}.letter{width:36px;height:36px;display:grid;place-items:center;background:#f2eef7;border-radius:11px;color:#78628f;font-weight:600;font-size:18px}.title{flex:1}.title h2{font-size:18px}.title small{color:#aaa2af;font-size:12px}.check{color:#857196;font-size:12px;visibility:hidden}.selected .check{visibility:visible}.stage{background:#f8f7fa;min-height:216px;display:flex;align-items:center;justify-content:center;padding:18px 12px;border-block:1px solid #f0edf3}.preview{display:block;width:285px;max-width:100%;height:auto;image-rendering:pixelated}.enlarged .preview{width:570px}.description{padding:16px 20px 18px}.description p{font-size:13px;margin:0 0 12px}.choice{display:inline-flex;align-items:center;gap:8px;cursor:pointer;color:#797181;font-size:14px}input{accent-color:#9474b3;margin:0;width:16px;height:16px}.full{margin-top:28px;background:white;border:1px solid #e6e1ea;border-radius:20px;padding:20px}.full header{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:16px}.home{display:block;width:100%;max-width:1024px;height:auto;margin:auto;border-radius:12px}.hint{font-size:12px}.footer{font-size:12px;color:#99909f;padding:22px 0}.theme-dark .stage{background:#22232a}.selected-title{color:#88709f;font-size:14px}@media(max-width:680px){main{margin:22px auto;padding:0 12px}h1{font-size:26px}.grid{grid-template-columns:1fr}.toolbar{gap:8px}.option header{padding:16px}.description{padding:14px 16px}.full{padding:12px}.full header{align-items:flex-start;flex-direction:column}.full h2{font-size:19px}}
</style></head><body><main><a href="home-ai-ball.html">← 上一版 100 px</a><div style="margin-top:24px" class="eyebrow">HOME / AI COMPANION</div><h1>四种比例，看看哪一版更顺眼</h1><p class="intro">保留靠左、暖白纯色与正面表情，调整小球的大小、位置和遮挡。下方可选择一版，查看它在完整首页里的效果。</p><span class="badge">4 版对比 · 未烧录</span><div class="toolbar"><div class="group" aria-label="主题"><button class="active" data-theme="light">浅色</button><button data-theme="dark">深色</button></div><select id="scene" aria-label="小球状态"><option value="thinking">思考 · 彩带环绕</option><option value="idle">待机</option></select><div class="group" aria-label="预览方式"><button class="active" data-motion="false">同帧对比</button><button data-motion="true">播放动画</button></div><div class="group" aria-label="预览大小"><button class="active" data-enlarged="false">卡片原尺寸</button><button data-enlarged="true">放大查看</button></div></div><div class="grid" id="grid"></div><section class="full"><header><h2>放回首页，看整体比例</h2><span class="selected-title" id="selected-title"></span></header><a id="full-link" target="_blank"><img class="home" id="home" alt="所选版本的原生 LVGL 首页"></a><p class="hint">点击图片打开 1024 × 600 原图。选择只改变此预览，确认后再应用。</p></section><p class="footer">浅色 / 深色、待机 / 思考均完成原生布局检查，对话入口回调检查通过。当前为独立布局预览，正式首页和固件未替换；动图每 100 ms 采样一次。<a href="home-ball-options/options.json">查看验证记录 ↗</a></p></main><script>
const options=__OPTIONS__;let selected='__SELECTED__',theme='light',motion=false,enlarged=false;const grid=document.querySelector('#grid'),scene=document.querySelector('#scene');
grid.innerHTML=options.map(o=>`<article class="option" data-id="${o.id}"><header><span class="letter">${o.id.toUpperCase()}</span><div class="title"><h2>${o.name}</h2><small>${o.diameter} px · 底部遮挡 ${o.id==='c'?'1/3':'1/4'}</small></div><span class="check">已选</span></header><div class="stage"><img class="preview" alt="${o.name} AI 卡片"></div><div class="description"><p>${o.description}</p><label class="choice"><input type="radio" name="variant" value="${o.id}">选这个版本</label></div></article>`).join('');
function render(){document.body.classList.toggle('theme-dark',theme==='dark');grid.classList.toggle('enlarged',enlarged);document.querySelectorAll('.option').forEach(el=>{const id=el.dataset.id;el.classList.toggle('selected',id===selected);el.querySelector('input').checked=id===selected;el.querySelector('img').src=`home-ball-options/${id}/${theme}-${scene.value}-card.${motion?'gif':'png'}`});const current=options.find(o=>o.id===selected);document.querySelector('#selected-title').textContent=selected.toUpperCase()+' · '+current.name;const url=`home-ball-options/${selected}/${theme}-${scene.value}.png`;document.querySelector('#home').src=url;document.querySelector('#full-link').href=url;document.querySelectorAll('[data-theme]').forEach(b=>b.classList.toggle('active',b.dataset.theme===theme));document.querySelectorAll('[data-motion]').forEach(b=>b.classList.toggle('active',(b.dataset.motion==='true')===motion));document.querySelectorAll('[data-enlarged]').forEach(b=>b.classList.toggle('active',(b.dataset.enlarged==='true')===enlarged));}
document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;render()});document.querySelectorAll('[data-motion]').forEach(b=>b.onclick=()=>{motion=b.dataset.motion==='true';render()});document.querySelectorAll('[data-enlarged]').forEach(b=>b.onclick=()=>{enlarged=b.dataset.enlarged==='true';render()});grid.addEventListener('change',e=>{selected=e.target.value;render()});scene.onchange=render;render();
</script></body></html>'''


if __name__ == '__main__':
    main()
