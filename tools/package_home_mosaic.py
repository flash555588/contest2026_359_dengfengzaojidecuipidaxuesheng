#!/usr/bin/env python3
"""Package the 96 px charcoal desktop mosaic with native review frames."""
from pathlib import Path
import argparse,hashlib,json,shutil
import numpy as np
from PIL import Image
def rgb(path):
    raw=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(600,1024).astype(np.uint32)
    return Image.fromarray(np.stack([((raw>>11)&31)*255//31,((raw>>5)&63)*255//63,(raw&31)*255//31],axis=2).astype('uint8'))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];desktop=root/'project/app/espdl-quickapp/overlay/apps/system/desktop';dest=a.output/'home-mosaic';dest.mkdir(exist_ok=True)
    cross=json.loads((root/'output/home-mosaic/cross/validation.json').read_text(encoding='utf-8'))
    for item in cross['sources']:assert item['sha256']==hashlib.sha256((desktop/item['source']).read_bytes()).hexdigest()
    assert 'GROK_EMBEDDED_NATIVE_OK' in (root/'output/home-mosaic/component-native.log').read_text(encoding='utf-8')
    screens=json.loads((a.native/'screens.json').read_text(encoding='utf-8'));assert len(screens)==18
    for entry in screens:
        src=a.native/entry['file'];shutil.copy2(src,dest/src.name);im=Image.open(src);im.crop((108,374,393,552)).save(dest/(src.stem+'-card.png'))
    for theme in ['light','dark']:
        for state in ['idle','thinking','replying']:
            frames=[rgb(path).crop((108,374,393,552)).quantize(colors=256,dither=Image.Dither.NONE) for path in sorted(a.native.glob(f'{theme}-{state}-frame-*.rgb565'))]
            assert len(frames)==30
            frames[0].save(dest/f'{theme}-{state}-card.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
        # Update the existing combined review's homepage to match the source.
        target=a.output/'espclaw-grok'/f'{theme}-home-grok.png'
        if target.parent.exists():shutil.copy2(dest/f'{theme}-mosaic-black-idle.png',target)
    black=np.array(Image.open(dest/'light-mosaic-black-idle-card.png'))[:89,:165]
    # Native pixel evidence of charcoal body, bright eyes and multiple shades.
    dark=(black[:,:,0]<100)&(black[:,:,1]<100)&(black[:,:,2]<100)
    assert int(dark.sum())>1800
    assert len(np.unique(black[dark],axis=0))>=3
    eye=black[25:80,40:116];assert int(np.all(eye>220,axis=2).sum())>20
    report={'source_integrated':True,'flashed':False,'board_tested':False,'firmware_linked':False,'target_object_compiles':cross['sources'],
        'desktop':{'diameter':96,'center':[76,65],'viewport':[0,0,285,89],'card':[108,374,285,178],'nominal_hidden_bottom':24,'body_cell':4,'eye_cell':2,'body':'#1A1A1A radial shade','grout':'#111111','eyes':'#F6F5F0','default_palette':'black in both UI themes'},
        'mini_chat':'unchanged 48 px original pixel palette and poses beside current reply','screens':18,'animations':6,'native_frames':180,
        'checks':['same A card and ball viewport','bright eyes and shaded charcoal tile pixels','black/ivory palette','light/dark desktop','eight chat-derived states','chat entry callback','native component state/lifecycle sanitizers','six RV32 object compiles'],
        'limits':'Offline native LVGL/QuickJS review. No new whole-firmware link, flash, device FPS, live backend or target stack validation.'}
    report['source_files']=[{'file':name,'sha256':hashlib.sha256((desktop/name).read_bytes()).hexdigest()} for name in ['glass_grok.c','glass_grok.h','glass_grok_resource.c','glass_home_ui.inc','qpk_polygon_draw.inc']]
    (dest/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (a.output/'home-mosaic.html').write_text(PAGE,encoding='utf-8');print('HOME_MOSAIC_REVIEW_OK: 96 px black mosaic; 18 native screenshots; 6 card animations; same A geometry; unflashed')

PAGE='''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>桌面 · 黑色马赛克球</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f4f6;color:#2d3038;font:15px/1.65 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1120px;margin:32px auto;padding:0 24px}h1{font-size:30px;font-weight:600;margin:12px 0}h2{font-size:19px;font-weight:600;margin:0}p{color:#888994;margin:9px 0}a{color:#776087;text-decoration:none}.badge{display:inline-block;padding:5px 12px;border-radius:20px;background:#eae6ee;color:#82728f;font-size:12px}.toolbar{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin:24px 0}.group{display:flex;gap:4px;background:#e9e7ed;padding:4px;border-radius:11px}button,select{font:inherit;color:#807986;cursor:pointer}button{border:0;border-radius:8px;padding:7px 14px;background:none}button.active{background:white;color:#51415e;box-shadow:0 1px 4px #32244312}select{padding:9px 12px;border:1px solid #ded9e4;border-radius:10px;background:white}section{background:white;border:1px solid #e2dde8;padding:20px;border-radius:20px;margin:20px 0}.home{display:block;width:100%;max-width:1024px;height:auto;margin:auto;border-radius:12px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin-top:20px}figure{margin:0;min-width:0}figcaption{font-size:13px;color:#908796;margin-top:12px}.stage{display:flex;justify-content:center;align-items:center;background:#f5f3f8;border-radius:12px;padding:20px 8px;min-height:218px}.card{display:block;width:285px;height:auto;max-width:100%;image-rendering:pixelated}.dark .stage{background:#24212a}.small{font-size:12px}.footer{font-size:12px;color:#99929e;padding:8px 0 24px}@media(max-width:670px){main{margin:20px auto;padding:0 12px}h1{font-size:24px}section{padding:12px}.pair{grid-template-columns:1fr}}
</style></head><body><main><a href="espclaw-grok.html">ESPClaw 迷你球 ↗</a><h1>黑色马赛克球，放回桌面</h1><p>保留 A 版 96 px 大小、靠左位置和底部 1/4 遮挡。球体改为炭黑色方块拼贴，保留明暗层次，配暗色缝隙、浅色眼睛和彩带。</p><span class="badge">已替换桌面源码 · 未烧录</span><div class="toolbar"><div class="group"><button class="active" data-theme="light">浅色桌面</button><button data-theme="dark">深色桌面</button></div><select id="state" aria-label="小球状态"><option value="idle">待机</option><option value="thinking" selected>思考</option><option value="replying">输出回复</option><option value="busy">处理草稿</option><option value="done">完成</option><option value="error">出错</option><option value="stopped">停止</option><option value="listening">聆听</option></select></div><section><a id="full" target="_blank"><img class="home" id="home" alt="桌面 A 版位置里的黑色马赛克球"></a><p class="small">原生 LVGL / QuickJS 渲染，点击查看 1024 × 600 原图。</p></section><section><h2>卡片原尺寸 · 同一球径</h2><div class="pair"><figure><div class="stage"><img class="card" id="black" alt="96 px 黑色马赛克球卡片"></div><figcaption>桌面已用此版 · 黑色 96 px</figcaption></figure><figure><div class="stage"><img class="card" id="white" alt="96 px 象牙白马赛克对照"></div><figcaption>同尺寸白色对照 · 待机画面</figcaption></figure></div><p class="small">待机、思考和回复有动画预览。卡片保持 285 × 178，小球上半区保持 285 × 89；聊天输出框旁仍用原来的 48 px 迷你像素球。</p></section><p class="footer">原生尺寸、遮挡、入口与生命周期检查通过，受影响的目标文件交叉编译通过。新整包固件尚未链接，未烧录，实板效果待验证。<a href="home-mosaic/validation.json">验证记录 ↗</a></p></main><script>
let theme='light';const state=document.querySelector('#state');function render(){const value=state.value,base='home-mosaic/'+theme+'-mosaic-black-'+value;document.querySelector('#home').src=base+'.png';document.querySelector('#full').href=base+'.png';document.querySelector('#black').src=['idle','thinking','replying'].includes(value)?'home-mosaic/'+theme+'-'+value+'-card.gif':base+'-card.png';document.querySelector('#white').src='home-mosaic/'+theme+'-mosaic-white-idle-card.png';document.body.classList.toggle('dark',theme==='dark');document.querySelectorAll('[data-theme]').forEach(b=>b.classList.toggle('active',b.dataset.theme===theme));}document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;render()});state.onchange=render;render();
</script></body></html>'''

if __name__=='__main__':main()
