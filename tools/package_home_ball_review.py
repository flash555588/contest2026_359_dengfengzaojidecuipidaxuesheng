#!/usr/bin/env python3
"""Package the native home-card prototype for visual approval."""
from pathlib import Path
import argparse,json,shutil,hashlib
import numpy as np
from PIL import Image

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];dest=a.output/'home-ai-ball';dest.mkdir(exist_ok=True)
    report=json.loads((a.native/'VALIDATION.json').read_text())
    assert not report['flashed'] and report['prototype_only']
    assert hashlib.sha256((root/'project/app/espdl-quickapp/overlay/apps/system/desktop/glass_home_ui.inc').read_bytes()).hexdigest()==report['production_home_sha256']
    assert 'HOME_BALL_NATIVE_OK' in (a.native/'native.log').read_text()
    animations=[]
    for theme in ['light','dark']:
        for scene in ['idle','thinking']:
            frames=[]
            for path in sorted(a.native.glob(f'{theme}-{scene}-*.rgb565')):
                raw=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(600,1024).astype(np.uint32)
                rgb=np.stack([((raw>>11)&31)*255//31,((raw>>5)&63)*255//63,(raw&31)*255//31],axis=2).astype('uint8')
                im=Image.fromarray(rgb)
                if path.stem.endswith('-012'):im.save(dest/f'{theme}-{scene}.png')
                frames.append(im.crop((108,374,393,552)).quantize(colors=256,dither=Image.Dither.NONE))
            assert len(frames)==30
            gif=dest/f'{theme}-{scene}-card.gif';frames[0].save(gif,save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
            animations.append({'theme':theme,'scene':scene,'file':gif.name,'native_frames':30,'capture_interval_ms':100})
        shutil.copy2(root/'output/home-ai-ball/size-80-front'/f'{theme}-thinking-012.png',dest/f'{theme}-before.png')
    report['animations']=animations;report['limits']='Isolated layout prototype: native LVGL home with vector scene replay, not full firmware runtime integration. Board unchanged. GIF capture interval 100 ms is for review, not device FPS.'
    (dest/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    document='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>首页 AI 卡片 · 中号小球嵌入</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f6f8;color:#28323a;font:15px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1120px;margin:32px auto;padding:0 24px}h1{font-size:29px;line-height:1.4}h2{font-size:20px}p{color:#7b8791}a{color:#64719b}.badge{display:inline-block;background:#e8f2ec;color:#3d7659;padding:5px 12px;border-radius:24px;font-size:13px}.toolbar{display:flex;gap:9px;flex-wrap:wrap;margin:22px 0}button,select{font:inherit;padding:8px 14px;border:1px solid #dce2e9;border-radius:10px;background:white;color:#65717b;cursor:pointer}button.active{background:#ece9f3;color:#695688;border-color:#ddd7eb}section{background:white;border:1px solid #e1e6ec;border-radius:20px;padding:20px;margin:20px 0}.home{display:block;width:100%;max-width:1024px;margin:auto;border-radius:12px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:18px}figure{margin:0;overflow:auto}.card{display:block;width:285px;height:178px;max-width:none;margin:20px auto;image-rendering:pixelated}figcaption{color:#7c8995;font-size:13px}.before{display:block;width:100%;border-radius:12px}.note{font-size:13px}@media(max-width:680px){main{padding:0 12px;margin:20px auto}h1{font-size:24px}.pair{grid-template-columns:1fr}section{padding:14px}}
</style><main><a href="grok-styles.html">小球样式审核 ↗</a><br><div class="badge">首页卡片布局预览 · 未烧录</div><h1>小球放大，重新调整卡片比例</h1><p>小球从 80 px 放大至 100 px，略向右上调整。保留靠左、正面、暖白纯色和底部约 1/4 遮挡，上方留白更紧凑。</p><div class="toolbar"><button class="active" data-theme="light">浅色</button><button data-theme="dark">深色</button><select id="scene" aria-label="小球状态"><option value="thinking">思考 · 彩带环绕</option><option value="idle">待机</option></select></div><section><a id="full" href="home-ai-ball/light-thinking.png" target="_blank"><img class="home" id="home" src="home-ai-ball/light-thinking.png" alt="首页 AI 卡片嵌入中号小球"></a><p class="note">原生 LVGL 渲染，可点击打开 1024 × 600 原图。</p></section><section><h2>卡片内的动态效果</h2><img class="card" id="card" src="home-ai-ball/light-thinking-card.gif" alt="AI 卡片内的小球动画"><p class="note">小球底部通过卡片内部边界遮挡，上方留白；彩带仍在卡片内绘制。此处按卡片原尺寸显示；动图每 100 ms 采样一次。</p></section><section><h2>首页前后对照</h2><div class="pair"><figure><img class="before" id="before" src="home-ai-ball/light-before.png" alt="上版 · 80 px"><figcaption>上版 · 80 px</figcaption></figure><figure><img class="before" id="after" src="home-ai-ball/light-thinking.png" alt="现在 · 100 px"><figcaption>现在 · 100 px</figcaption></figure></div></section><section><h2>审核状态</h2><p>浅色、深色均完成原生布局检查，对话按钮回调保留。当前是独立效果预览，正式首页和固件未替换，未烧录。</p><a href="home-ai-ball/validation.json">验证记录 ↗</a></section></main><script>
let theme='light';const scene=document.querySelector('#scene');function render(){const base='home-ai-ball/'+theme+'-'+scene.value;document.querySelector('#home').src=base+'.png';document.querySelector('#after').src=base+'.png';document.querySelector('#full').href=base+'.png';document.querySelector('#card').src=base+'-card.gif';document.querySelector('#before').src='home-ai-ball/'+theme+'-before.png';document.querySelectorAll('[data-theme]').forEach(b=>b.classList.toggle('active',b.dataset.theme===theme));}document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;render();});scene.onchange=render;
</script></html>'''
    (a.output/'home-ai-ball.html').write_text(document,encoding='utf-8');print('HOME_AI_CARD_REVIEW_OK four native home images; four card animations; callbacks and clipping checked; unflashed')
if __name__=='__main__':main()
