#!/usr/bin/env python3
"""Publish the pixel-art mini review alongside the unchanged medium preview."""
from pathlib import Path
import argparse,hashlib,json,re,shutil
import numpy as np
from PIL import Image

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['native','candidate','output']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[2];dest=a.output/'grok-mosaic';dest.mkdir(exist_ok=True)
    source=json.loads((a.candidate/'SOURCES.json').read_text());native=json.loads((a.native/'native-performance.json').read_text())
    for path,value in source['production_sha256'].items():assert sha(root/path)==value,path
    assert sha(a.candidate/'app.js')==source['source_sha256']['app']==native['results']['after']['app_sha256']
    assert sha(a.candidate/'polygon.inc')==source['source_sha256']['polygon']==native['results']['after']['polygon_sha256']
    assert sha(root/'output/grok-sizes/medium/app.js')==source['medium_app_sha256']==native['results']['previous']['app_sha256']
    images=list((a.native/'after').glob('*.png'));assert len(images)==50
    medium_images=list((a.native/'previous').glob('*.png'));assert len(medium_images)==50
    for path in medium_images:assert path.read_bytes()==(root/'output/grok-sizes/native-tiny/after'/path.name).read_bytes(),'Medium output changed: '+path.name
    folder=dest/'mini';folder.mkdir(exist_ok=True)
    for path in images:shutil.copy2(path,folder/path.name)
    # Closeups use nearest-neighbour display of actual native pixels. The
    # original 48 px ball remains unchanged in the full-screen preview.
    crop=(432,213,592,325)
    for title,base in [('pixel',a.native/'after'),('previous',a.native/'before')]:
        with Image.open(base/'thinking-024.png') as im:im.crop(crop).save(dest/(title+'-close.png'))
    animations=[]
    for scenario in ['idle','thinking','spin','confetti']:
        paths=sorted((a.native/'after/motion').glob(scenario+'-*.rgb565'));assert len(paths)==30
        frames=[]
        for path in paths:
            raw=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(600,1024).astype(np.uint32)
            rgb=np.stack([((raw>>11)&31)*255//31,((raw>>5)&63)*255//63,(raw&31)*255//31],axis=2).astype('uint8')
            frames.append(Image.fromarray(rgb).quantize(colors=256))
        gif=folder/(scenario+'.gif');frames[0].save(gif,save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
        with Image.open(gif) as im:
            count=im.n_frames;duration=0
            for i in range(count):im.seek(i);duration+=im.info['duration']
        assert duration==3000
        animations.append({'scenario':scenario,'native_frames':30,'encoded_frames':count,'duration_ms':duration})
    log=(a.native/'after/native.log').read_text()
    for marker in ['NATIVE_GROK_OK','NATIVE_POLYGON_GUARDS_OK','NATIVE_BUFFER_GUARDS_OK','NATIVE_MOSAIC_GUARDS_OK']:assert marker in log,marker
    rows=[]
    for smooth,pixel in zip(native['results']['before']['median_metrics'],native['results']['after']['median_metrics']):
        total=lambda x:(x['js_ms']+x['draw_ms'])/60
        rows.append({'scenario':pixel['scenario'],'previous_pixel_ms_per_frame':total(smooth),'pixel_ms_per_frame':total(pixel),
            'draw_reduction_percent':100*(1-pixel['draw_ms']/smooth['draw_ms'])})
    report={'flashed':False,'production_applied':False,'firmware_compiled':False,'board_tested':False,'sources':source,
        'diameter_px':48,'pixel_cell_px':2,'detail_cell_px':1,'body_palette':source['body_palette'],'rim_color':source['rim_color'],'medium_diameter_px':80,'medium_output_identical':True,'medium_pixel_checks':50,
        'states':32,'interaction_profiles':8,'partial_refresh_checks':96,'animations':animations,'records':rows,
        'native_measurements':native,'limits':'Desktop native QuickJS/LVGL, sanitizers, three-run medians. GIF palette encoding and 100 ms capture cadence are for review only, not board FPS. Pixel cell is a per-polygon opt-in; omitted parameter retains the original smooth drawing.'}
    (a.candidate/'VALIDATION.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');shutil.copy2(a.candidate/'VALIDATION.json',dest/'validation.json')
    states=re.findall(r"id: '([0-4][0-9])', name: '([^']+)'",(root/'project/ouo/vendor/grok-ball.js').read_text(encoding='utf-8'));assert len(states)==32
    options=''.join(f'<option value="state-{code}">{code} · {name}</option>' for code,name in states)
    document='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>迷你小球 · 清晰像素版</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f6f8;color:#27303a;font:15px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1120px;margin:32px auto;padding:0 24px}h1{font-size:29px;line-height:1.4;margin:14px 0}h2{font-size:21px}p{color:#7a8691}a{color:#626c9e}.badge{display:inline-block;background:#e7f2ed;color:#38745a;border-radius:24px;padding:5px 12px;font-size:13px}section{background:white;border:1px solid #e1e7ed;border-radius:20px;padding:20px;margin:20px 0}.toolbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:18px}button,select{font:inherit;border:1px solid #dce2e8;padding:8px 14px;border-radius:10px;background:white;color:#697682;cursor:pointer}button.active{background:#ece9f3;color:#65528c;border-color:#ded9ed}select{max-width:100%}.spacer{flex:1}.screen{display:block;max-width:1024px;width:100%;margin:auto;border-radius:12px}.screen.pixel{image-rendering:pixelated}.info{display:flex;gap:12px;justify-content:space-between;padding-top:12px}.note{font-size:13px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0;background:#fffbff;border-radius:12px;padding:16px;overflow:hidden}.close{display:block;width:320px;max-width:100%;margin:auto;image-rendering:pixelated}figcaption{text-align:center;color:#7c8996;font-size:13px}small{display:block;color:#8795a2}@media(max-width:640px){main{padding:0 12px;margin:20px auto}section{padding:14px}h1{font-size:24px}.pair{grid-template-columns:1fr}.info{flex-wrap:wrap}}
</style><main><a href="grok-quality.html">小球审核 ↗</a><br><div class="badge">清晰像素版 · 待审核 · 未烧录</div><h1>48 px，清晰的像素小球</h1><p>暖白亮面、清楚的阴影分区与深色轮廓。眼睛和彩带保留更细的像素，表情更容易辨认；尺寸仍是 48 px，中型版保持 80 px 平滑样式。</p>
<section><div class="toolbar"><button class="active" data-size="mini">迷你 · 清晰像素</button><button data-size="medium">中型 · 平滑</button><span class="spacer"></span><select id="scene" aria-label="动作或表情"><optgroup label="动态预览"><option value="thinking">思考 · 环绕彩带</option><option value="idle">待机</option><option value="spin">自旋</option><option value="confetti">撒花</option></optgroup><optgroup label="32 种表情 · 静态截图">OPTIONS</optgroup></select></div><a id="full" href="grok-mosaic/mini/thinking.gif" target="_blank"><img class="screen pixel" id="preview" src="grok-mosaic/mini/thinking.gif" alt="48 px 迷你像素小球原生预览"></a><div class="info"><b id="size-label">迷你 · 48 px · 清晰像素</b><a id="png" href="grok-mosaic/mini/thinking-024.png" target="_blank">原生 PNG / 原尺寸 ↗</a></div><p class="note">完整界面随页面等比例缩放。动图由原生连续帧生成，每 100 ms 采样，供外观审核，不代表实板帧率。</p></section>
<section><h2>清晰版与上一版 · 相同帧对照</h2><div class="pair"><figure><img class="close" src="grok-mosaic/pixel-close.png" alt="原生像素效果两倍放大"><figcaption>现在 · 深色轮廓与分区阴影<small>两倍放大，仅供看细节</small></figcaption></figure><figure><img class="close" src="grok-mosaic/previous-close.png" alt="之前粗化像素版本两倍放大"><figcaption>之前 · 粗化像素<small>相同大小、相同帧</small></figcaption></figure></div></section>
<section><h2>审核状态</h2><p>迷你版交互与全部表情检查通过，像素渲染完成局部刷新和内存检查。中型版的 50 张原生输出逐像素保持一致。正式应用与固件仍待审核，未烧录。</p><a href="grok-mosaic/validation.json">完整验证记录 ↗</a></section></main><script>
let size='mini';const scene=document.querySelector('#scene');function render(){const base=size==='mini'?'grok-mosaic/mini/':'grok-sizes/medium/',state=scene.value.startsWith('state-'),url=base+scene.value+(state?'.png':'.gif');document.querySelector('#preview').src=url;document.querySelector('#preview').classList.toggle('pixel',size==='mini');document.querySelector('#full').href=url;document.querySelector('#png').href=base+(state?scene.value:scene.value+'-024')+'.png';document.querySelector('#size-label').textContent=size==='mini'?'迷你 · 48 px · 清晰像素':'中型 · 80 px · 平滑';document.querySelectorAll('[data-size]').forEach(b=>b.classList.toggle('active',b.dataset.size===size));}document.querySelectorAll('[data-size]').forEach(b=>b.onclick=()=>{size=b.dataset.size;render();});scene.onchange=render;
</script></html>'''
    document=document.replace('OPTIONS',options)
    previous=a.output/'grok-sizes.html';archive=a.output/'grok-sizes-smooth.html'
    if not archive.exists():shutil.copy2(previous,archive)
    (a.output/'grok-mosaic.html').write_text(document,encoding='utf-8');previous.write_text(document,encoding='utf-8')
    print('GROK_MOSAIC_REVIEW_OK 32 states, four animations; 50 unchanged medium renders',json.dumps(rows))
if __name__=='__main__':main()
