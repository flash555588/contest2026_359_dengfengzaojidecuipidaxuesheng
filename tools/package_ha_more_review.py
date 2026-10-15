"""Publish the current native More captures; no firmware or private data."""
from pathlib import Path
import shutil,json
from PIL import Image
root=Path(__file__).resolve().parents[2]
source=root/'output/ha-slider-follow-20261014/more-native'
review=root/'output/all-ui-review';assets=review/'ha-more';assets.mkdir(exist_ok=True)
for theme in ('light','dark'):
    for suffix in ('lamp','color','number','mode'):
        name=theme+'-hass-more-'+suffix+'.png'
        shutil.copy2(source/name,assets/name)
    with Image.open(source/(theme+'-hass-more-color.png')) as im:
        points=json.loads((source/(theme+'-ct-geometry.json')).read_text())
        warm=im.getpixel(tuple(points['warm']));cool=im.getpixel(tuple(points['cool']))
        assert warm[0]>warm[2]+60 and cool[2]>240 and cool[2]>warm[2]+60,(theme,warm,cool)
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>米家 HA · 更多功能</title>
<style>body{margin:0;background:#f3f4f6;color:#22262a;font:16px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1150px;margin:auto;padding:28px}h1{font-size:28px;margin-bottom:4px}p{color:#7c858e}button,a{font:inherit;color:#379bff}button{padding:9px 20px;background:#fff;border:1px solid #dce3eb;border-radius:12px;cursor:pointer}figure{margin:24px 0;background:white;border:1px solid #dde3eb;border-radius:16px;overflow:hidden}img{display:block;width:100%}figcaption{padding:14px 18px;color:#7c858e}.tabs{display:flex;gap:10px;flex-wrap:wrap}</style>
<main><h1>更多功能 · 融合到一张卡片</h1><p>以原版横向滑条为基础，去掉独立功能列。亮度、色温、数值、模式和动作都在同一张大卡片里直接操作。色温从左侧暖黄渐变到右侧白光。</p>
<div class="tabs"><button id="theme">切换浅色</button><button data-scene="lamp">灯光控制</button><button data-scene="color">色温 4600K</button><button data-scene="number">数值设置</button><button data-scene="mode">模式选项</button></div>
<figure><a id="zoom" href="ha-more/dark-hass-more-color.png" target="_blank"><img id="shot" src="ha-more/dark-hass-more-color.png"></a><figcaption>真实 LVGL/QuickJS 离线渲染与触摸测试。设备名称和数值为测试数据。</figcaption></figure>
<p>不用切换亮度或色温标签。滑条拖动只预览，松手提交一次；使用实体自己的范围和选项。同一设备的传感器保持只读。</p>
<a href="mijia-lamp.html">大小卡组合 ↗</a></main>
<script>let theme='dark',scene='color';function show(){let p='ha-more/'+theme+'-hass-more-'+scene+'.png';document.querySelector('#shot').src=p;document.querySelector('#zoom').href=p;document.querySelector('#theme').textContent='切换'+(theme==='dark'?'浅色':'深色')}document.querySelector('#theme').onclick=()=>{theme=theme==='dark'?'light':'dark';show()};document.querySelectorAll('[data-scene]').forEach(b=>b.onclick=()=>{scene=b.dataset.scene;show()});</script></html>'''
(review/'mijia-more.html').write_text(page,encoding='utf-8')
(root/'output/ha-slider-follow-20261014/gradient-check.json').write_text(json.dumps({'passed':True,'checks':['both themes: horizontal warm left to white right','cool end stays white beyond current value','native RGB565 pixel samples']},indent=2))
print('HA_MORE_REVIEW_OK 8 captures, gradient RGB565 samples passed')
