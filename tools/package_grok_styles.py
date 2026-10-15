#!/usr/bin/env python3
"""Publish native pixel/mosaic sprites at actual size and integer zoom only."""
from pathlib import Path
import argparse,hashlib,json,re,shutil
import numpy as np
from PIL import Image

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ['native','candidate','output']:p.add_argument('--'+k,type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[2];dest=a.output/'grok-styles';dest.mkdir(exist_ok=True)
    sources=json.loads((a.candidate/'SOURCES.json').read_text());native=json.loads((a.native/'native-performance.json').read_text())
    for path,value in sources['production_sha256'].items():assert sha(root/path)==value,path
    animations=[]
    for name,kind,bottom in [('pixel','previous',325),('mosaic','after',341)]:
        variant=next(v for v in sources['variants'] if v['name']==name)
        assert native['results'][kind]['app_sha256']==sha(a.candidate/name/'app.js')==variant['app_sha256']
        assert native['results'][kind]['polygon_sha256']==sources['polygon_sha256']==sha(a.candidate/'polygon.inc')
        folder=dest/name;folder.mkdir(exist_ok=True);crop=(432,213,592,bottom)
        images=list((a.native/kind).glob('*.png'));assert len(images)==50
        for path in images:
            shutil.copy2(path,folder/path.name)
            if not path.stem.endswith('probe'):
                with Image.open(path) as im:im.crop(crop).save(folder/(path.stem+'-sprite.png'))
        for scenario in ['idle','thinking','spin','confetti']:
            paths=sorted((a.native/kind/'motion').glob(scenario+'-*.rgb565'));assert len(paths)==30
            frames=[]
            for path in paths:
                raw=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(600,1024).astype(np.uint32)
                rgb=np.stack([((raw>>11)&31)*255//31,((raw>>5)&63)*255//63,(raw&31)*255//31],axis=2).astype('uint8')
                frames.append(Image.fromarray(rgb).crop(crop).quantize(colors=256,dither=Image.Dither.NONE))
            gif=folder/(scenario+'-sprite.gif');frames[0].save(gif,save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
            with Image.open(gif) as im:
                duration=0
                for i in range(im.n_frames):im.seek(i);duration+=im.info['duration']
            assert duration==3000
            animations.append({'variant':name,'scenario':scenario,'native_frames':30,'duration_ms':duration,'crop':crop,'dithering':False})
        assert 'NATIVE_GROK_OK' in (a.native/kind/'native.log').read_text()
    report={'flashed':False,'production_applied':False,'firmware_compiled':False,'board_tested':False,'sources':sources,
        'states_per_variant':32,'interaction_profiles_per_variant':8,'partial_refresh_checks':96,'animations':animations,
        'preview_scale':'actual-size 1x by default; optional integer nearest-neighbour 2x or 4x; no fractional resize',
        'native_measurements':native,'limits':'Native desktop QuickJS/LVGL with sanitizers. GIF frames sampled every 100 ms; crop may omit particles beyond the sphere area. Full native PNG available for every selected state. Not board FPS or power measurement.'}
    (a.candidate/'VALIDATION.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');shutil.copy2(a.candidate/'VALIDATION.json',dest/'validation.json')
    states=re.findall(r"id: '([0-4][0-9])', name: '([^']+)'",(root/'project/ouo/vendor/grok-ball.js').read_text(encoding='utf-8'));assert len(states)==32
    options=''.join(f'<option value="state-{code}">{code} · {name}</option>' for code,name in states)
    cards=''.join(f'<article><div class="card-title"><h2>{title}</h2><span>{diameter} px</span></div><p>{desc}</p><div class="stage"><img class="sprite" id="sprite-{name}" src="grok-styles/{name}/thinking-sprite.gif" alt="{title}原生动画"></div><a id="full-{name}" href="grok-styles/{name}/thinking-024.png" target="_blank">查看完整原生界面 ↗</a></article>' for name,title,diameter,desc in [('pixel','像素小球',48,'细像素轮廓 · 紧凑、轻巧'),('mosaic','马赛克小球',80,'方块拼贴 · 明显的色块与细缝')])
    document='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>像素小球 / 马赛克小球</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f6f8;color:#28323a;font:15px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1080px;margin:36px auto;padding:0 24px}h1{font-size:30px;line-height:1.4;margin:16px 0}h2{font-size:20px;margin:0}p{color:#7b8791}a{color:#62719c}.badge{display:inline-block;background:#e9f2ec;color:#407456;border-radius:24px;padding:5px 12px;font-size:13px}.toolbar{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin:24px 0}select{font:inherit;max-width:100%;border:1px solid #dae1e8;border-radius:10px;padding:9px 13px;background:white;color:#65717e}.pair{display:grid;grid-template-columns:1fr 1fr;gap:20px}article,.box{background:white;border:1px solid #e1e6ed;border-radius:20px;padding:22px}.card-title{display:flex;align-items:center;justify-content:space-between}.card-title span{color:#98a2aa;font-size:13px}.stage{position:relative;overflow:auto;height:calc(32px + 128px * var(--zoom,1));background:#fffbff;border-radius:12px;margin:24px 0 18px}.sprite{position:absolute;top:16px;left:50%;transform:translateX(-50%);width:calc(160px * var(--zoom,1));max-width:none;image-rendering:pixelated}.box{margin-top:24px}.note{font-size:13px}@media(max-width:680px){main{padding:0 12px;margin:24px auto}h1{font-size:25px}.pair{grid-template-columns:1fr}article,.box{padding:16px}}
</style><main><a href="grok-quality.html">小球审核 ↗</a><br><div class="badge">两种独立效果 · 待审核 · 未烧录</div><h1>一个像素，一个马赛克</h1><p>迷你版用细像素，保留细轮廓和眼睛；中型版用方块拼贴，突出马赛克的色块感。两版都保留 32 种表情与彩带动画。</p><div class="toolbar"><select id="scene" aria-label="动作或表情"><optgroup label="动态预览"><option value="thinking">思考 · 环绕彩带</option><option value="idle">待机</option><option value="spin">自旋</option><option value="confetti">撒花</option></optgroup><optgroup label="32 种表情 · 原生截图">OPTIONS</optgroup></select><select id="zoom" aria-label="预览倍率"><option value="1">1× 原尺寸</option><option value="2">2× 看细节</option><option value="4">4× 看细节</option></select></div><div class="pair">CARDS</div><p class="note">默认按原尺寸显示小球。放大只使用整数倍率，保持像素边缘。动画近景可能裁去远离球体的粒子；完整截图可检查整体位置与遮挡。</p><section class="box"><h2>审核状态</h2><p>两版的表情和交互检查通过，已完成原生渲染、局部刷新与内存检查。正式应用和固件尚未替换，未烧录。</p><a href="grok-styles/validation.json">验证记录 ↗</a></section></main><script>
const scene=document.querySelector('#scene');function render(){const state=scene.value.startsWith('state-');for(const name of ['pixel','mosaic']){document.querySelector('#sprite-'+name).src='grok-styles/'+name+'/'+scene.value+'-sprite.'+(state?'png':'gif');document.querySelector('#full-'+name).href='grok-styles/'+name+'/'+(state?scene.value:scene.value+'-024')+'.png';}}scene.onchange=render;function alignPixels(){for(const im of document.querySelectorAll('.sprite')){const stage=im.parentElement,r=stage.getBoundingClientRect(),width=im.getBoundingClientRect().width;im.style.transform='none';im.style.left=(Math.round(r.left+(stage.clientWidth-width)/2)-r.left)+'px';im.style.top=(Math.round(r.top+16)-r.top)+'px';}}document.querySelector('#zoom').onchange=e=>{document.documentElement.style.setProperty('--zoom',e.target.value);alignPixels();};const observer=new ResizeObserver(alignPixels);document.querySelectorAll('.stage').forEach(s=>observer.observe(s));alignPixels();
</script></html>'''
    document=document.replace('OPTIONS',options).replace('CARDS',cards)
    for page in ['grok-styles.html','grok-sizes.html']:(a.output/page).write_text(document,encoding='utf-8')
    print('GROK_TWO_STYLES_REVIEW_OK pixel 48 px; mosaic 80 px; actual-size sprites, integer zoom; eight animations')
if __name__=='__main__':main()
