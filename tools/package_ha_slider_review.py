"""Present unmodified LVGL screenshots and tightly cropped reference states."""
from pathlib import Path
from PIL import Image
import shutil,json
root=Path(__file__).resolve().parents[2];source=root/'output/ha-slider-follow-20261014'
review=root/'output/all-ui-review';assets=review/'ha-slider';assets.mkdir(parents=True,exist_ok=True)
for p in (source/'native').glob('*hass-*.png'):shutil.copy2(p,assets/p.name)
for theme in ['light','dark']:
    with Image.open(assets/(theme+'-hass-lamp-small.png')) as im:
        im.crop((180,139,447,329)).save(assets/(theme+'-76.png'))
        im.crop((460,139,727,329)).save(assets/(theme+'-16.png'))
for p in source.glob('board-*.png'):shutil.copy2(p,assets/p.name)
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>米家 HA · 统一开关与卡片编辑</title><style>*{box-sizing:border-box}body{margin:0;background:#f4f5f7;color:#22262b;font:15px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}
main{max-width:1160px;margin:auto;padding:32px 24px 64px}h1{font-size:28px;margin:0 0 8px}h2{font-size:19px;margin:30px 0 14px}p{margin:0 0 22px;color:#7c858e}
a{color:#3189d7;text-decoration:none}button{background:#fff;border:1px solid #e2e6eb;border-radius:10px;padding:10px 18px;font:inherit;cursor:pointer;color:#3189d7}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:20px}figure{margin:0;background:#fff;border-radius:16px;overflow:hidden;border:1px solid #e2e6eb}img{display:block;width:100%}figcaption{padding:12px 18px;color:#7c858e;font-size:13px}.crop img{width:267px;max-width:100%;margin:20px auto;image-rendering:auto}
@media(max-width:760px){.grid{grid-template-columns:1fr}main{padding:24px 16px}}
</style><main><h1>米家 HA · 统一开关与卡片编辑</h1><p>大小卡片共用圆形开关。点击「编辑卡片」，在固定网格里选择每个设备的大小，点击「完成编辑」查看组合。保留原有设备功能和卡片样式。</p>
<button id="theme">切换浅色</button> <a href="mijia.html">查看原有卡片样式 ↗</a>
<h2>常用卡片在首页</h2><figure><a data-link="hass-common-home.png" href="ha-slider/dark-hass-common-home.png" target="_blank"><img data-tail="hass-common-home.png" src="ha-slider/dark-hass-common-home.png"></a><figcaption>按打开、控制次数排序。常用小卡优先，大小卡继续组合；操作中不跳位，返回总览时更新排序。</figcaption></figure>
<h2>指令等待时，其他卡片继续操作</h2><figure><a data-link="hass-control-waiting.png" href="ha-slider/dark-hass-control-waiting.png" target="_blank"><img data-tail="hass-control-waiting.png" src="ha-slider/dark-hass-control-waiting.png"></a><figcaption>原生触摸测试：台灯等待回执，床头灯仍可拖动到 50%，显示待执行。下一条指令在前一条确认后发送一次。</figcaption></figure>
<h2>统一开关 · 大小卡组合</h2><figure><a data-link="hass-lamp-mixed.png" href="ha-slider/dark-hass-lamp-mixed.png" target="_blank"><img data-tail="hass-lamp-mixed.png" src="ha-slider/dark-hass-lamp-mixed.png"></a><figcaption>同一圆形开关、图标尺寸和边距，亮度与色温滑条保留原有控制</figcaption></figure>
<h2>编辑卡片 · 固定位置切换尺寸</h2><figure><a data-link="hass-lamp-editor.png" href="ha-slider/dark-hass-lamp-editor.png" target="_blank"><img data-tail="hass-lamp-editor.png" src="ha-slider/dark-hass-lamp-editor.png"></a><figcaption>每张卡片都有明确按钮，切换后保持位置；完成编辑后显示混排效果</figcaption></figure>
<h2>16% 与 76% · 原生尺寸</h2><div class="grid">
<figure class="crop"><img data-tail="16.png" src="ha-slider/dark-16.png"><figcaption>16% · 保留圆角，白条位于橙色块内部</figcaption></figure>
<figure class="crop"><img data-tail="76.png" src="ha-slider/dark-76.png"><figcaption>76% · 同一白条位置规则</figcaption></figure></div>
<h2>整体与拖动</h2><div class="grid"><figure><a data-link="hass-lamp-small.png" href="ha-slider/dark-hass-lamp-small.png" target="_blank"><img data-tail="hass-lamp-small.png" src="ha-slider/dark-hass-lamp-small.png"></a><figcaption>灯具小卡 · 真实 LVGL 离线渲染</figcaption></figure>
<figure><a data-link="hass-lamp-small-drag.png" href="ha-slider/dark-hass-lamp-small-drag.png" target="_blank"><img data-tail="hass-lamp-small-drag.png" src="ha-slider/dark-hass-lamp-small-drag.png"></a><figcaption>60 次连续原生指针移动 · 滑条显示 80%</figcaption></figure></div>
__BOARD__</main><script>let theme='dark';document.querySelector('#theme').onclick=()=>{theme=theme==='dark'?'light':'dark';document.querySelectorAll('[data-tail]').forEach(im=>im.src='ha-slider/'+theme+'-'+im.dataset.tail);document.querySelectorAll('[data-link]').forEach(a=>a.href='ha-slider/'+theme+'-'+a.dataset.link);document.querySelector('#theme').textContent='切换'+(theme==='dark'?'浅色':'深色')};</script></html>'''
hardware=list(source.glob('board-*.png'))
board='<h2>实际板卡</h2>'+''.join('<figure><img src="ha-slider/'+p.name+'"><figcaption>实际 framebuffer · 当前 HA 数据</figcaption></figure>' for p in hardware) if hardware else ''
(review/'mijia-lamp.html').write_text(page.replace('__BOARD__',board),encoding='utf-8')
print('HA_SLIDER_REVIEW_OK',12,'native captures',len(hardware),'board captures')
