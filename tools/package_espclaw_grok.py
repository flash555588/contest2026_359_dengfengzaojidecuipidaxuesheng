#!/usr/bin/env python3
"""Package the original pixel mini beside the current native AI output."""
from pathlib import Path
import argparse,hashlib,json,re,shutil
import numpy as np
from PIL import Image
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];desktop=root/'project/app/espdl-quickapp/overlay/apps/system/desktop';dest=a.output/'espclaw-grok';dest.mkdir(exist_ok=True)
    cross=json.loads((root/'output/grok-espclaw/cross/validation.json').read_text(encoding='utf-8'))
    for item in cross['sources']:assert item['sha256']==hashlib.sha256((desktop/item['source']).read_bytes()).hexdigest()
    assert 'GROK_EMBEDDED_NATIVE_OK' in (root/'output/grok-espclaw/component-native.log').read_text(encoding='utf-8')
    assert 'all 32 original IDs' in (root/'output/grok-espclaw/component-native.log').read_text(encoding='utf-8')
    assert 'CHAT_ACTIVITY_HOST_OK' in (root/'output/grok-espclaw/chat-activity.log').read_text(encoding='utf-8')
    screens=json.loads((a.native/'screens.json').read_text(encoding='utf-8'));assert len(screens)==20
    for row in screens:shutil.copy2(a.native/row['file'],dest/row['file'])
    animations=[]
    positions=json.loads((a.native/'avatar-positions.json').read_text(encoding='utf-8'))
    for theme in ['light','dark']:
        for state in ['thinking','replying','listening']:
            frames=[]
            box=next(p['box'] for p in positions if p['theme']==theme and p['state']==state)
            x,y,w,h=box;assert x==250 and w==64 and h==64 and 0<=y<=536
            for path in sorted(a.native.glob(f'{theme}-{state}-frame-*.rgb565')):
                raw=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(600,1024).astype(np.uint32)
                rgb=np.stack([((raw>>11)&31)*255//31,((raw>>5)&63)*255//63,(raw&31)*255//31],axis=2).astype('uint8')
                frames.append(Image.fromarray(rgb).crop((x,y,x+w,y+h)).quantize(colors=256,dither=Image.Dither.NONE))
            assert len(frames)==30
            frames[0].save(dest/f'{theme}-{state}-avatar.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
            animations.append({'theme':theme,'state':state,'frames':30})
    report={'source_integrated':True,'flashed':False,'board_tested':False,'firmware_linked':False,'target_objects_compiled':cross['sources'],'avatar':{'body_diameter':48,'viewport':[248,12,64,64],'body_cell':1,'detail_cell':1,'fill':'#F2EFE8','occlusion':0},'homepage':'Selected A layout retained: 96 px, center 76/65, 24 px nominal bottom occlusion','native_screens':20,'animations':animations,'renderer':'real production UI, QuickJS Grok engine and LVGL; offline service fixtures','checks':['typed service event projection','stream state transitions','completion once, no celebration on reopening stored history','voice listening','hidden and obscured pause','reduced motion','independent contexts','repeated creation/deletion with ASan/UBSan and leak checks','original entry/navigation and 32 Grok expressions regression','six ESP32-P4 target object compiles'],'limits':'No new whole-firmware link, device performance or live network/voice validation. Host QuickJS stack allowance differs from RV32; board verification remains pending approval.'}
    report['avatar']={'body_diameter':48,'viewport_size':[64,64],'body_cell':1,'detail_cell':1,'style':'original-pixel-mini','fill':'upstream radial shading with original pixel palette','pose':'original upstream expression poses','occlusion':0,'placement':'left of latest AI output; follows scrolling; welcome anchor when no output','positions':positions,'persistent_component':True}
    report['checks']+=['reply alignment and scrolling','editor hides companion','message rebuilding retains component']
    registered=[{'id':id,'name':name,'group':group} for id,name,group in re.findall(r"id: '(\d{2})', name: '([^']+)', group: '([^']+)'",(root/'project/ouo/vendor/grok-ball.js').read_text(encoding='utf-8'))]
    assert len(registered)==len({item['id'] for item in registered})==32
    report['expressions']={'registered':registered,'count':32,'tested_styles':['black pixel 96 px','ivory pixel 96 px','original pixel mini 48 px'],'id_compatibility':'numeric, padded and unpadded strings; invalid IDs fall back to 02','driver':'real service stream callbacks -> activity snapshot -> native reducer -> embedded engine','conversation_ids':['02','30','31','32','33','34','35','36','39','41']}
    report['checks']+=['all 32 expressions rendered in three component styles','real service callbacks drive original IDs','padded string IDs and invalid-ID fallback']
    report['files']=[{'source':str(p.relative_to(root)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [desktop/name for name in ['glass_grok.c','glass_grok.h','glass_grok_state.c','glass_grok_resource.c','glass_chat_ui.inc','glass_home_ui.inc','glass_chat.h','glass_chat_service.c','qpk_polygon.inc','qpk_polygon_draw.inc','qpk_polygon_convert.inc','qpk_limits.h','Makefile']]]
    (dest/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (a.output/'espclaw-grok.html').write_text(PAGE,encoding='utf-8');print('ESPCLAW_GROK_REVIEW_OK: original pixel mini beside output; 20 native screens, 6 animations, target objects; unflashed')

PAGE='''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESPClaw · 标题旁的小球头像</title><style>
*{box-sizing:border-box}body{margin:0;background:#f3f6f5;color:#243c37;font:15px/1.65 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1120px;margin:32px auto;padding:0 24px}h1{font-size:29px;font-weight:600;margin:12px 0}h2{font-size:18px;font-weight:600;margin:0}p{color:#84958f;margin:9px 0}a{color:#3c8475;text-decoration:none}.badge{display:inline-block;background:#e3efea;color:#4f8877;border-radius:20px;padding:5px 12px;font-size:12px}.toolbar{display:flex;flex-wrap:wrap;gap:12px;margin:24px 0;align-items:center}.group{display:flex;gap:4px;padding:4px;background:#e8eeeb;border-radius:11px}button,select{font:inherit;color:#71867e;cursor:pointer}button{border:0;background:none;border-radius:8px;padding:7px 14px}button.active{background:white;color:#376555;box-shadow:0 1px 4px #24403510}select{padding:9px 12px;border:1px solid #dce5df;border-radius:10px;background:white}section{background:white;border:1px solid #dde7e1;border-radius:20px;padding:20px;margin:20px 0}.screen{display:block;width:100%;max-width:1024px;margin:auto;border-radius:12px}.animation{display:flex;gap:24px;align-items:center}.avatar{width:128px;height:128px;image-rendering:pixelated;background:#f6f8f7;border-radius:14px}.dark .avatar{background:#101b20}.small{font-size:12px}.footer{font-size:12px;padding:8px 0 24px;color:#96a39d}@media(max-width:600px){main{padding:0 12px;margin:22px auto}h1{font-size:24px}section{padding:12px}.animation{align-items:flex-start;gap:16px}.avatar{width:96px;height:96px}}
</style></head><body><main><a href="home-ball-expressions.html">首页 A 版表情 ↗</a><h1>小球移到「对话」标题旁</h1><p>48 px 的完整小头像，没有紫色外框和底部遮挡。左栏恢复原布局，表情跟随 ESPClaw 的聆听、思考、回复和任务状态。</p><span class="badge">已接入源码 · 未烧录</span><div class="toolbar"><div class="group"><button class="active" data-theme="light">浅色</button><button data-theme="dark">深色</button></div><select id="state" aria-label="审核状态"><option value="idle">待机</option><option value="thinking">思考</option><option value="replying">输出回复</option><option value="busy">处理草稿</option><option value="done">完成</option><option value="error">出错</option><option value="stopped">停止</option><option value="listening">语音聆听</option><option value="input">输入界面</option><option value="home">首页 A 版</option></select></div><section><a id="full" target="_blank"><img class="screen" id="screen" alt="融合小球后的原生 ESPClaw 页面"></a><p class="small">原生 LVGL / QuickJS 页面；使用离线服务数据审核效果。点击图片打开原图。</p></section><section id="animation"><div class="animation"><img class="avatar" id="avatar" alt="标题小球的原生动画"><div><h2>头像动画</h2><p>放大 2 倍查看像素细节。<br>思考、输出回复和聆听可查看动画。</p></div></div></section><p class="footer">已验证状态切换、完成动画只播放一次、退出释放、后台暂停和减少动效。受影响的 6 个源文件通过 ESP32-P4 目标编译；新整包固件尚未链接，联网及实板性能尚未测试。<a href="espclaw-grok/validation.json">验证记录 ↗</a></p></main><script>
let theme='light';const state=document.querySelector('#state');function render(){const value=state.value,base='espclaw-grok/'+theme+'-'+(value==='home'?'home-grok':'chat-grok-'+value)+'.png';document.querySelector('#screen').src=base;document.querySelector('#full').href=base;const animated=['thinking','replying','listening'].includes(value);document.querySelector('#animation').hidden=!animated;if(animated)document.querySelector('#avatar').src='espclaw-grok/'+theme+'-'+value+'-avatar.gif';document.body.classList.toggle('dark',theme==='dark');document.querySelectorAll('[data-theme]').forEach(b=>b.classList.toggle('active',b.dataset.theme===theme));}document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;render()});state.onchange=render;render();
</script></body></html>'''

PAGE=PAGE.replace('ESPClaw · 标题旁的小球头像','ESPClaw · 输出框旁的迷你小球').replace('小球移到「对话」标题旁','小球放回 AI 输出框旁边')
PAGE=PAGE.replace('48 px 的完整小头像，没有紫色外框和底部遮挡。左栏恢复原布局，表情跟随 ESPClaw 的聆听、思考、回复和任务状态。','复用之前的 48 px 迷你像素球，保留明暗层次、彩带和原始表情动作。小球在当前回复框左侧，随输出一起滚动；标题和左栏恢复原样。')
PAGE=PAGE.replace('标题小球的原生动画','原有迷你像素球动画').replace('头像动画','迷你球动画').replace('<option value="replying">','<option value="replying" selected>')
PAGE=PAGE.replace('<span class="badge">','<p class="small">原来全部 32 个表情已注册；聊天按真实服务状态切换表情，完成后回到待机。</p><span class="badge">',1)

if __name__=='__main__':main()
