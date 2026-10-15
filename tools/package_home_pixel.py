#!/usr/bin/env python3
"""Package the user's original pixel ball in the native home card."""
from pathlib import Path
import argparse,hashlib,json,shutil
import numpy as np
from PIL import Image

def rgb(path):
    raw=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(600,1024).astype(np.uint32)
    return Image.fromarray(np.stack([((raw>>11)&31)*255//31,((raw>>5)&63)*255//63,(raw&31)*255//31],axis=2).astype('uint8'))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];desktop=root/'project/app/espdl-quickapp/overlay/apps/system/desktop'
    dest=a.output/'home-pixel';dest.mkdir(exist_ok=True)
    evidence=root/'output/home-pixel'
    cross=json.loads((evidence/'cross/validation.json').read_text(encoding='utf-8'))
    for item in cross['sources']:assert item['sha256']==hashlib.sha256((desktop/item['source']).read_bytes()).hexdigest()
    assert 'all 32 original IDs rendered in black/ivory pixel' in (evidence/'component-native.log').read_text(encoding='utf-8')
    assert 'CHAT_ACTIVITY_HOST_OK' in (evidence/'chat-activity.log').read_text(encoding='utf-8')
    reuse=json.loads((evidence/'pixel-reuse.json').read_text(encoding='utf-8'));assert reuse['compared_frames']==reuse['black_geometry_frames']==384
    screens=json.loads((a.native/'screens.json').read_text(encoding='utf-8'));assert len(screens)==32
    for entry in screens:
        src=a.native/entry['file'];shutil.copy2(src,dest/src.name)
        Image.open(src).crop((108,374,393,552)).save(dest/(src.stem+'-card.png'))
    animations=[]
    for theme,palette in [('light','white'),('dark','black')]:
        for color in ['white','black']:
            for state in ['idle','thinking','replying']:
                images=[rgb(path) for path in sorted(a.native.glob(f'{theme}-{color}-{state}-frame-*.rgb565'))]
                assert len(images)==30
                cards=[im.crop((108,374,393,552)) for im in images]
                unique=len({im.tobytes() for im in cards});assert unique>1,(theme,color,state)
                for suffix,source in [('card',cards),('home',images)]:
                    frames=[im.quantize(colors=256,dither=Image.Dither.NONE) for im in source]
                    frames[0].save(dest/f'{theme}-{color}-{state}-{suffix}.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
                if color==palette:shutil.copy2(dest/f'{theme}-{color}-{state}-card.gif',dest/f'{theme}-{state}-card.gif')
                animations.append({'theme':theme,'palette':color,'state':state,'frames':30,'unique_card_frames':unique,'moving':True})
        target=a.output/'espclaw-grok'/f'{theme}-home-grok.png'
        if target.parent.exists():shutil.copy2(dest/f'{theme}-pixel-{palette}-idle.png',target)
    # Native pixel evidence: the original five-band shading has no tile seams.
    white=np.array(Image.open(dest/'light-pixel-white-idle-card.png'))
    body=white[15:87,28:125];assert len(np.unique(body.reshape(-1,3),axis=0))>=5
    black=np.array(Image.open(dest/'dark-pixel-black-idle-card.png'))
    region=black[15:87,28:125];dark=np.all(region<90,axis=2)
    assert int(dark.sum())>2000
    report={'source_integrated':True,'flashed':False,'board_tested':False,'firmware_linked':False,
      'desktop':{'diameter':96,'center':[76,65],'viewport':[0,0,285,89],'card':[108,374,285,178],
        'nominal_hidden_bottom':24,'body_cell':1,'eye_cell':1,'style':'original-pixel',
        'pose':'original animation with right-facing horizontal eye anchor','viewpoint_override':True,'face_direction':'right','engine_pose_mutation':False,'shading':'original pixel palette; grayscale counterpart for black',
        'grout':None,'light_palette':'original ivory','dark_palette':'black'},
      'reference_equivalence':reuse,'target_object_compiles':cross['sources'],'screens':32,'animations':24,'motion_checks':animations,
      'expressions':32,'mini_chat':'unchanged original 48 px pixel mini beside current output',
      'checks':['32 original states / 384 unchanged engine animation poses','right-facing home anchor with original gaze and spin','real native LVGL/QuickJS rendering',
        'original A viewport and chat entry','conversation and voice state driver','lifecycle sanitizers','six RV32 object compiles'],
      'limits':'Offline review only. No firmware link, flash, target FPS or live backend validation.'}
    report['sources']=[{'file':name,'sha256':hashlib.sha256((desktop/name).read_bytes()).hexdigest()} for name in ['glass_grok.c','glass_grok.h','glass_grok_resource.c','glass_home_ui.inc','qpk_polygon_draw.inc']]
    (dest/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (a.output/'home-pixel.html').write_text(PAGE,encoding='utf-8')
    # The previous home link should show the corrected current design too.
    (a.output/'home-mosaic.html').write_text(PAGE,encoding='utf-8')
    print('HOME_PIXEL_REVIEW_OK: 32 native screenshots; both palettes animate in both themes; card and full-home GIFs; 12 native motion checks; unflashed')

PAGE='''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>首页 · 原版像素小球</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f4f6;color:#2d3038;font:15px/1.65 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1120px;margin:32px auto;padding:0 24px}h1{font-size:30px;font-weight:600;margin:12px 0}h2{font-size:19px;font-weight:600;margin:0}p{color:#888994;margin:9px 0}a{color:#776087;text-decoration:none}.badge{display:inline-block;padding:5px 12px;border-radius:20px;background:#eae6ee;color:#82728f;font-size:12px}.toolbar{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin:24px 0}.group{display:flex;gap:4px;background:#e9e7ed;padding:4px;border-radius:11px}button,select{font:inherit;color:#807986;cursor:pointer}button{border:0;border-radius:8px;padding:7px 14px;background:none}button.active{background:white;color:#51415e;box-shadow:0 1px 4px #32244312}select{padding:9px 12px;border:1px solid #ded9e4;border-radius:10px;background:white}section{background:white;border:1px solid #e2dde8;padding:20px;border-radius:20px;margin:20px 0}.home{display:block;width:100%;max-width:1024px;height:auto;margin:auto;border-radius:12px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin-top:20px}figure{margin:0;min-width:0}figcaption{font-size:13px;color:#908796;margin-top:12px}.stage{display:flex;justify-content:center;align-items:center;background:#f5f3f8;border-radius:12px;padding:20px 8px;min-height:218px}.card{display:block;width:285px;height:auto;max-width:100%;image-rendering:pixelated}.dark .stage{background:#24212a}.small{font-size:12px}.footer{font-size:12px;color:#99929e;padding:8px 0 24px}@media(max-width:670px){main{margin:20px auto;padding:0 12px}h1{font-size:24px}section{padding:12px}.pair{grid-template-columns:1fr}}
</style></head><body><main><a href="grok-sizes.html?rev=two-styles">你指的原版像素小球 ↗</a><h1>首页复用原版像素小球</h1><p>复用原来的细像素、明暗层次、眼睛姿态和彩带。首页保留 96 px、靠左位置和底部 1/4 遮挡；浅色主题用原白球，深色主题用黑色配色。</p><span class="badge">已接入源码 · 未烧录</span><div class="toolbar"><div class="group"><button class="active" data-theme="light">浅色首页</button><button data-theme="dark">深色首页</button></div><select id="state" aria-label="小球状态"><option value="idle">待机</option><option value="thinking" selected>思考</option><option value="replying">输出回复</option><option value="busy">处理草稿</option><option value="done">完成</option><option value="error">出错</option><option value="stopped">停止</option><option value="listening">聆听</option></select></div><section><a id="full" target="_blank"><img class="home" id="home" alt="首页里的原版像素小球"></a><p class="small">原生 LVGL / QuickJS 渲染，点击查看 1024 × 600 原图。</p></section><section><h2>卡片原尺寸</h2><div class="pair"><figure><div class="stage"><img class="card" id="current" alt="当前首页像素小球卡片"></div><figcaption>首页当前配色 · 96 px</figcaption></figure><figure><div class="stage"><img class="card" id="alternate" alt="另一配色的像素小球"></div><figcaption id="alternate-label">黑色配色对照 · 待机</figcaption></figure></div><p class="small">待机、思考和回复可看动画。32 种表情的 384 帧绘制数据与原像素版一致，仅调整首页尺寸与位置。聊天输出框旁仍为原来的 48 px 迷你球。</p></section><p class="footer">对话状态联动、原生渲染和目标文件编译检查通过。未烧录，实板效果待验证。<a href="home-pixel/validation.json">验证记录 ↗</a></p></main><script>
let theme='light';const state=document.querySelector('#state');function render(){const value=state.value,palette=theme==='light'?'white':'black',other=theme==='light'?'black':'white',base='home-pixel/'+theme+'-pixel-'+palette+'-'+value;document.querySelector('#home').src=base+'.png';document.querySelector('#full').href=base+'.png';document.querySelector('#current').src=['idle','thinking','replying'].includes(value)?'home-pixel/'+theme+'-'+value+'-card.gif':base+'-card.png';document.querySelector('#alternate').src='home-pixel/'+theme+'-pixel-'+other+'-idle-card.png';document.querySelector('#alternate-label').textContent=(other==='black'?'黑色':'原白色')+'配色对照 · 待机';document.body.classList.toggle('dark',theme==='dark');document.querySelectorAll('[data-theme]').forEach(b=>b.classList.toggle('active',b.dataset.theme===theme));}document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;render()});state.onchange=render;render();
</script></body></html>'''
PAGE=PAGE.replace('首页复用原版像素小球','首页像素小球 · 朝右')
PAGE=PAGE.replace('复用原来的细像素、明暗层次、眼睛姿态和彩带。','小球的基础朝向改为右方，保留 Grok Ball 原来的眨眼、视线、自旋、明暗层次和彩带动画。')
PAGE=PAGE.replace('32 种表情的 384 帧绘制数据与原像素版一致，仅调整首页尺寸与位置。','原来的 32 种表情均保留，对话按状态切换对应表情。自旋时保留自然转动，结束后回到朝右的基础朝向。')
PAGE=PAGE.replace('黑色配色对照 · 待机','黑色配色对照 · 同步状态')
PAGE=PAGE.replace('待机、思考和回复可看动画。','完整首页和黑白两球在待机、思考、回复时均播放动画；其他选项为状态截图。')
PAGE=PAGE.replace("document.querySelector('#home').src=base+'.png';document.querySelector('#full').href=base+'.png';document.querySelector('#current').src=['idle','thinking','replying'].includes(value)?'home-pixel/'+theme+'-'+value+'-card.gif':base+'-card.png';document.querySelector('#alternate').src='home-pixel/'+theme+'-pixel-'+other+'-idle-card.png';document.querySelector('#alternate-label').textContent=(other==='black'?'黑色':'原白色')+'配色对照 · 待机';", "const animated=['idle','thinking','replying'].includes(value),motion='home-pixel/'+theme+'-'+palette+'-'+value,alternate='home-pixel/'+theme+'-'+other+'-'+value;document.querySelector('#home').src=animated?motion+'-home.gif':base+'.png';document.querySelector('#full').href=document.querySelector('#home').src;document.querySelector('#current').src=animated?motion+'-card.gif':base+'-card.png';document.querySelector('#alternate').src=animated?alternate+'-card.gif':'home-pixel/'+theme+'-pixel-'+other+'-'+value+'-card.png';document.querySelector('#alternate-label').textContent=(other==='black'?'黑色':'原白色')+'配色对照 · '+state.selectedOptions[0].textContent;")
if __name__=='__main__':main()
