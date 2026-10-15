"""Show current HA native and hardware captures beside the retained style catalog."""
from pathlib import Path
import json,shutil

root=Path(__file__).resolve().parents[2]
source=root/'output/mijia-style-binding-20261014'
review=root/'output/all-ui-review';assets=review/'mijia-current';assets.mkdir(parents=True,exist_ok=True)
for p in (source/'native').glob('*.png'):shutil.copy2(p,assets/p.name)
for p in source.glob('board-*.png'):shutil.copy2(p,assets/p.name)
hardware=[p.name for p in source.glob('board-*.png')]
items=[('实板 · 当前设备组合',hardware[0])] if hardware else []
items+=[('LVGL · 组合与拖动 · 离线示例','light-hass-grouped-mixed.png'),
        ('LVGL · 拖动到 80% 的预览','light-hass-grouped-drag.png'),
        ('LVGL · 更多操作 · 主控制','light-hass-grouped-more.png'),
        ('LVGL · 更多操作 · 同设备传感器','light-hass-grouped-sensor.png')]
html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>米家 · 样式与设备组合</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f5f7;color:#22262b;font:15px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}
main{max-width:1240px;margin:auto;padding:32px 24px 64px}h1{font-size:28px;margin:0 0 8px}p{color:#7c858e;margin:0 0 22px}
a{color:#3189d7;text-decoration:none}.actions{display:flex;gap:16px;margin:18px 0 32px}button,.actions a{background:#fff;border:1px solid #e2e6eb;border-radius:10px;padding:10px 18px;font:inherit;color:#3887c7;cursor:pointer}
table{width:100%;border-collapse:collapse;background:#fff;border-radius:14px;overflow:hidden;margin-bottom:28px}td,th{text-align:left;padding:14px 20px;border-bottom:1px solid #eff1f4}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:20px}figure{margin:0;background:#fff;border-radius:16px;overflow:hidden;border:1px solid #e2e6eb}figure:first-child{grid-column:1/-1}
img{display:block;width:100%}figcaption{padding:12px 18px;color:#7c858e;font-size:13px}@media(max-width:760px){.grid{grid-template-columns:1fr}td,th{padding:10px}main{padding:24px 16px}}
</style><main><h1>米家 · 样式与设备组合</h1><p>59 个品类 · 127 条属性组合 · 25 种参考版式保留。每台设备自选大小，按功能匹配原有卡片。</p>
<div class="actions"><a href="mijia.html">查看全部 127 种样式 ↗</a><button id="theme">离线渲染切换深色</button></div>
<table><tr><th>当前设备</th><th>组合后的卡片</th><th>保留的功能实体</th></tr>
<tr><td>台灯</td><td>亮度＋色温 · 大卡可直接拖动</td><td>37</td></tr>
<tr><td>摄像机</td><td>摄像机主开关 · 指示灯为独立功能</td><td>100</td></tr>
<tr><td>门锁</td><td>门状态＋电量 · 辅助开关留在更多操作</td><td>39</td></tr></table>
<div class="grid">'''
for title,file in items:
    html+=f'<figure><a href="mijia-current/{file}" target="_blank"><img loading="lazy" src="mijia-current/{file}" alt="{title}" data-file="{file}"></a><figcaption>{title}</figcaption></figure>'
html+='''</div><p style="margin-top:24px">实板画面使用当前 HA 数据；其他画面来自真实 LVGL 与 QuickJS 的离线示例。原接口未支持的控制保持只读，未提供视频流时不会生成摄像机假画面。</p></main><script>
let dark=false;document.querySelector('#theme').onclick=()=>{dark=!dark;document.querySelectorAll('[data-file]').forEach(im=>{const f=im.dataset.file;if(!f.startsWith('light-'))return;im.src='mijia-current/'+f.replace('light-',dark?'dark-':'light-');im.parentElement.href=im.src});document.querySelector('#theme').textContent='离线渲染切换'+(dark?'浅色':'深色')};
</script></html>'''
(review/'mijia-current.html').write_text(html,encoding='utf-8')
print('HA_CARD_REVIEW_OK',len(items),'screens',len(hardware),'hardware')
