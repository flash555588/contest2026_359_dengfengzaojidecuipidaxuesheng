#!/usr/bin/env python3
"""Package actual LVGL PNGs into a local, offline review gallery."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import zipfile
from PIL import Image, ImageDraw, ImageFont


def category(key):
    if key.startswith('ouo'): return 'Grok Ball 表情'
    if key.startswith('files'): return '文件管理'
    if key.startswith(('wifi','bluetooth','ble','mouse','portal')): return '连接与配置'
    if key.startswith(('music','weather','environment')): return '音乐与天气'
    if key.startswith('chat'): return 'AI 助手'
    if key.startswith('hass'): return '智能家居'
    if key.startswith(('live','local-video','camera','espdl','recorder')): return '影音与识别'
    if key.startswith(('hello','game2048','pet')): return '快应用与桌宠'
    return '桌面与系统'


HTML = r'''<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LVGL · 全界面审核</title>
<style>
*{box-sizing:border-box}body{margin:0;color:#22302e;background:#f3f5f4;font-family:"Segoe UI","Microsoft YaHei",sans-serif}button,input,select{font:inherit}button,a{touch-action:manipulation}button{cursor:pointer}header{padding:32px 36px 24px;background:#fff;border-bottom:1px solid #dfe5e2}.eyebrow{color:#687773;letter-spacing:2px;font-size:11px;font-weight:700}h1{font-size:30px;letter-spacing:-1px;margin:8px 0 12px;font-weight:600}header p{font-size:13px;color:#6c7975;line-height:1.7;margin:0;max-width:940px}.stats{display:flex;gap:8px;margin-top:18px;flex-wrap:wrap}.stats span{border:1px solid #dde5e1;border-radius:5px;padding:5px 10px;font-size:11px;color:#53645d}.toolbar{display:flex;gap:12px;align-items:center;flex-wrap:wrap;background:#f3f5f4;padding:18px 36px;position:sticky;top:0;z-index:3;border-bottom:1px solid #e0e7e3}input{width:270px;border:1px solid #d7e0db;border-radius:7px;padding:10px 12px;background:#fff;outline-color:#3a7664}select{padding:10px;background:#fff;border:1px solid #d7e0db;border-radius:7px;max-width:180px}.switch{display:flex;background:#e5ebe7;border-radius:7px;padding:3px}.switch button{border:0;border-radius:5px;background:transparent;padding:8px 14px;color:#66746d;font-size:13px}.switch .active{background:#fff;color:#183e31;box-shadow:0 1px 4px #163b3020}#count{font-size:12px;color:#728179;margin-left:auto}main{padding:24px 36px 60px}section{margin-bottom:32px}.sectionhead{display:flex;gap:10px;align-items:center;margin-bottom:14px}h2{font-size:17px;font-weight:600;margin:0}.sectionhead span{font-size:11px;color:#8b9790}.sectionhead a{margin-left:auto;font-size:12px;color:#3f705e;text-decoration:none}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.card{background:#fff;border:1px solid #dde5df;border-radius:9px;overflow:hidden;box-shadow:0 2px 8px #203b2910}.cardhead{display:flex;align-items:center;gap:12px;padding:13px 16px;border-bottom:1px solid #ebf0ec}.num{font-size:11px;color:#9ca8a0;font-variant-numeric:tabular-nums}.cardhead strong{font-size:13px;font-weight:500}.cardhead small{margin-left:auto;color:#a2aca7;font-size:10px}.shots{display:flex;gap:1px;background:#e5ebe7}.shot{border:0;background:#fff;padding:0;margin:0;display:block;width:100%;text-align:left;cursor:zoom-in}.shot img{display:block;width:100%;aspect-ratio:1024/600;object-fit:contain;background:#eef2ef}.shot figcaption{font-size:11px;color:#78877f;padding:8px 16px}.compare .shots .shot{width:50%}.empty{padding:60px;text-align:center;color:#74827a}footer{border-top:1px solid #dce4de;padding:24px 36px;color:#8b9790;font-size:11px}footer a{color:#3f705e}#viewer{position:fixed;inset:0;background:#101a16ec;z-index:10;display:none;align-items:center;justify-content:center;padding:68px 64px}#viewer.open{display:flex}#viewer img{max-width:100%;max-height:100%;object-fit:contain;box-shadow:0 14px 80px #0008}#viewerbar{position:absolute;left:32px;right:32px;top:18px;display:flex;align-items:center;gap:16px;color:#e4ece7;font-size:13px}#viewerbar a{color:#b4d3c1;margin-left:auto;font-size:12px;text-decoration:none}#viewerbar button,.arrow{background:#ffffff10;border:1px solid #ffffff30;color:#edf4ef;border-radius:6px;padding:9px 12px}.arrow{position:absolute;top:50%;font-size:23px;cursor:pointer}.prev{left:12px}.next{right:12px}#viewerhint{position:absolute;bottom:21px;color:#8daba0;font-size:11px}body.locked{overflow:hidden}@media(min-width:1700px){.grid{grid-template-columns:repeat(3,minmax(0,1fr))}}@media(max-width:760px){header{padding:24px 18px}.toolbar{padding:14px 18px}main{padding:20px 18px}input{width:100%}.grid{grid-template-columns:1fr}#count{margin-left:0}#viewer{padding:80px 12px}.arrow{top:auto;bottom:12px}#viewerhint{display:none}.cardhead small{display:none}}
</style>
<header><div class="eyebrow">LVGL / SCREEN REVIEW</div><h1>全界面审核</h1><p>复用现有二级界面。截图来自实际 LVGL 与快应用代码的离线渲染；网络、天气、文件和设备数据为审核示例。视频与相机页显示离线状态，未连接硬件、未刷机。</p><div class="stats"><span id="states"></span><span id="total"></span><span>原图 1024 × 600</span><span>统一线性图标</span><span>深 / 浅色桌面主题</span></div></header>
<div class="toolbar"><input id="search" type="search" placeholder="搜索页面或状态" aria-label="搜索页面"><select id="category" aria-label="筛选分类"><option value="">全部功能</option></select><div class="switch"><button class="active" data-theme="浅色">浅色</button><button data-theme="深色">深色</button><button data-theme="对比">并排对比</button></div><span id="count"></span></div>
<main id="content"></main><footer>深浅色指桌面主题；应用内保留原有配色。<a href="screens.json" download>页面清单</a> · <a href="review-info.json" download>渲染记录</a> · 点击截图查看原图，方向键切换。</footer>
<div id="viewer" role="dialog" aria-label="原尺寸截图" aria-modal="true"><div id="viewerbar"><span id="viewtitle"></span><a id="original" target="_blank" rel="noopener">打开原图 ↗</a><button id="close" aria-label="关闭原图">关闭 ×</button></div><button class="arrow prev" aria-label="上一张">‹</button><img id="viewimage" alt=""><button class="arrow next" aria-label="下一张">›</button><div id="viewerhint">1024 × 600 · ← → 切换 · Esc 关闭</div></div>
<script>
const DATA=__DATA__,SHEETS=__SHEETS__;
const categories=[...new Set(DATA.map(r=>r.category))],ids=[...new Set(DATA.map(r=>r.id))];
const number=new Map(ids.map((id,i)=>[id,String(i+1).padStart(2,'0')]));
let theme='浅色',activeRows=[],current=0;
const $=s=>document.querySelector(s),esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
$('#states').textContent=ids.length+' 个页面 / 状态';$('#total').textContent=DATA.length+' 张实际截图';
categories.forEach(c=>{const o=document.createElement('option');o.value=c;o.textContent=c;$('#category').append(o)});
function render(){const query=$('#search').value.trim().toLowerCase(),category=$('#category').value;
 const filtered=DATA.filter(r=>(!category||r.category===category)&&(!query||(r.title+' '+r.id+' '+r.category).toLowerCase().includes(query)));
 activeRows=filtered.filter(r=>theme==='对比'||r.theme===theme);let html='';
 const visibleIds=[...new Set(activeRows.map(r=>r.id))];$('#count').textContent=visibleIds.length+' 个状态 · '+activeRows.length+' 张';
 categories.forEach(c=>{const group=visibleIds.filter(id=>activeRows.find(r=>r.id===id).category===c);if(!group.length)return;
 const sheet=SHEETS.find(s=>s.category===c&&s.theme===(theme==='深色'?'深色':'浅色'));
 html+='<section><div class="sectionhead"><h2>'+esc(c)+'</h2><span>'+group.length+' 个状态</span>'+(sheet?'<a href="'+sheet.file+'" target="_blank">拼图总览 ↗</a>':'')+'</div><div class="grid">';
 group.forEach(id=>{const shots=activeRows.filter(r=>r.id===id),row=shots[0];html+='<article class="card '+(theme==='对比'?'compare':'')+'"><div class="cardhead"><span class="num">'+number.get(id)+'</span><strong>'+esc(row.title)+'</strong><small>'+esc(id)+'</small></div><div class="shots">';
 shots.forEach(r=>{const i=activeRows.indexOf(r);html+='<button class="shot" data-shot="'+i+'" aria-label="查看'+esc(r.title+' '+r.theme)+'"><img src="'+r.file+'" loading="lazy" alt="'+esc(r.title+' '+r.theme)+'"><figcaption>'+r.theme+'主题 · 点击放大</figcaption></button>'});html+='</div></article>'});html+='</div></section>'});
 $('#content').innerHTML=html||'<div class="empty">没有匹配的页面</div>';
}
function show(i){if(!activeRows.length)return;current=(i+activeRows.length)%activeRows.length;const r=activeRows[current];$('#viewimage').src=r.file;$('#viewimage').alt=r.title+' '+r.theme;$('#viewtitle').textContent=number.get(r.id)+' / '+r.title+' · '+r.theme;$('#original').href=r.file;$('#viewer').classList.add('open');document.body.classList.add('locked');$('#close').focus()}
function close(){$('#viewer').classList.remove('open');document.body.classList.remove('locked')}
$('#content').onclick=e=>{const b=e.target.closest('[data-shot]');if(b)show(Number(b.dataset.shot))};
$('.prev').onclick=()=>show(current-1);$('.next').onclick=()=>show(current+1);$('#close').onclick=close;
$('#viewer').onclick=e=>{if(e.target===$('#viewer'))close()};
document.onkeydown=e=>{if(!$('#viewer').classList.contains('open'))return;if(e.key==='Escape')close();else if(e.key==='ArrowLeft')show(current-1);else if(e.key==='ArrowRight')show(current+1)};
$('#search').oninput=render;$('#category').onchange=render;
document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;document.querySelectorAll('[data-theme]').forEach(x=>x.classList.toggle('active',x===b));render()});render();
</script></html>'''

CHANGES = r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>本轮修改 · LVGL 审核</title><style>
*{box-sizing:border-box}body{margin:0;background:#f3f5f4;color:#29382f;font-family:"Segoe UI","Microsoft YaHei",sans-serif}header{padding:26px 32px;background:white;border-bottom:1px solid #dce5df;position:sticky;top:0;z-index:2}h1{font-size:25px;margin:0 0 10px;font-weight:600}header p{font-size:12px;color:#7b8980;margin:0 0 16px}button,a{font:inherit}button{padding:8px 18px;border:1px solid #dce4df;border-radius:6px;cursor:pointer;background:#fff;color:#688072}.active{background:#3f6554;color:#fff;border-color:#3f6554}header a{float:right;color:#507a64;font-size:13px;text-decoration:none;padding:8px}main{padding:24px 32px 48px;max-width:1640px;margin:auto}section{margin-bottom:32px}h2{font-size:19px;font-weight:600;margin:0 0 8px}section p{font-size:12px;color:#849287;margin:0 0 14px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:18px}.frame{background:#fff;border:1px solid #dfe6e1;border-radius:8px;overflow:hidden}.label{padding:12px 16px;font-size:12px;color:#7e8e83;border-bottom:1px solid #edf1ee}.label strong{color:#436650;font-weight:500}img{width:100%;display:block;aspect-ratio:1024/600;object-fit:contain}.frame a{display:block;cursor:zoom-in}#metric{margin-bottom:12px}@media(max-width:740px){.pair{grid-template-columns:1fr}header,main{padding-left:16px;padding-right:16px}}
</style><header><a href="index.html">全部界面 ↗</a><h1>本轮修改审核</h1><p>控制面板 · 系统性能 · 天气卡片 · 米家入口、详情与设置。两侧均为实际 LVGL 截图，数据为离线审核示例，未刷机。点击图片打开原图。</p><button class="active" data-theme="light">浅色</button> <button data-theme="dark">深色</button></header><main>
<section><h2>01 / 控制面板</h2><p>左侧导航、独立设置卡片、统一图标与控件层级。</p><div class="pair" data-page="settings"></div></section>
<section><h2>02 / 系统性能</h2><p>参考 Windows 任务管理器：侧栏缩略曲线、CPU 与内存切换、最近 60 次有效采样。</p><div id="metric"><button class="active" data-metric="resources-history">CPU</button> <button data-metric="resources-memory">内存</button></div><div class="pair" data-page="resources"></div></section>
<section><h2>03 / 天气卡片</h2><p>温度主卡、天气图标、湿度、空气质量、PM2.5 与更新状态。</p><div class="pair" data-page="weather-loaded"></div></section>
<section><h2>04 / 米家入口页</h2><p>快应用分片创建界面，审核脚本原先在零帧时截取，入口页为空；现在推进 40 帧后再截图。</p><div class="pair" data-page="hass"></div></section><section><h2>05 / 米家详情页对齐</h2><p>详情页电源圆环、图标与提示文字原先比设备名称偏左 18 px；现统一居中于 364 px。</p><div class="pair" data-page="hass-device-more"></div></section><section><h2>06 / 米家连接设置</h2><p>设置按钮为池化控件，创建顺序先于面板而被不透明面板遮住；调整创建顺序后 7 个设置项恢复可见。</p><div class="pair" data-page="hass-settings"></div></section></main><script>let theme='light',metric='resources-history';function render(){document.querySelectorAll('[data-page]').forEach(e=>{const id=e.dataset.page,newId=id==='resources'?metric:id;const old='before-'+theme+'-'+id+'.png',fresh=theme+'-'+newId+'.png';e.innerHTML='<div class="frame"><div class="label">原版</div><a href="'+old+'" target="_blank"><img src="'+old+'" alt="原版界面"></a></div><div class="frame"><div class="label"><strong>修改后</strong> · 1024 × 600</div><a href="'+fresh+'" target="_blank"><img src="'+fresh+'" alt="修改后的 LVGL 界面"></a></div>'})}document.querySelectorAll('[data-theme]').forEach(b=>b.onclick=()=>{theme=b.dataset.theme;document.querySelectorAll('[data-theme]').forEach(x=>x.classList.toggle('active',x===b));render()});document.querySelectorAll('[data-metric]').forEach(b=>b.onclick=()=>{metric=b.dataset.metric;document.querySelectorAll('[data-metric]').forEach(x=>x.classList.toggle('active',x===b));render()});render();</script></html>'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--font',type=Path,default=Path('C:/Windows/Fonts/msyh.ttc'))
    args=parser.parse_args();out=args.directory.resolve()
    rows=json.loads((out/'screens.json').read_text(encoding='utf-8'))
    ids=list(dict.fromkeys(r['id'] for r in rows));categories=[]
    for r in rows:
        r['category']=category(r['id'])
        if r['category'] not in categories: categories.append(r['category'])
        with Image.open(out/r['file']) as im:
            assert im.size==(1024,600) and im.mode=='RGB', r['file']
        r['sha256']=hashlib.sha256((out/r['file']).read_bytes()).hexdigest()
    (out/'screens.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    sheets=[]
    for theme,prefix in [('浅色','light'),('深色','dark')]:
        for n,c in enumerate(categories,1):
            group=[r for r in rows if r['category']==c and r['theme']==theme]
            columns=3;tile_w=384;tile_h=225;gap=18;pad=28;header=124;label_h=52
            height=header+math.ceil(len(group)/columns)*(tile_h+label_h+gap)+pad
            sheet=Image.new('RGB',(pad*2+columns*tile_w+(columns-1)*gap,height),'#f3f5f4')
            draw=ImageDraw.Draw(sheet)
            font=lambda size:ImageFont.truetype(str(args.font),size)
            draw.text((pad,22),f'{n:02d} / {c}',fill='#20362b',font=font(29))
            draw.text((pad,70),f'{theme}桌面主题 · {len(group)} 个页面 / 状态 · 实际 LVGL 渲染 · 原图 1024 × 600',fill='#65776c',font=font(16))
            for i,r in enumerate(group):
                x=pad+(i%columns)*(tile_w+gap);y=header+(i//columns)*(tile_h+label_h+gap)
                draw.rounded_rectangle((x-1,y-1,x+tile_w,y+tile_h+label_h),radius=6,fill='#ffffff',outline='#d9e2dc')
                with Image.open(out/r['file']) as im:sheet.paste(im.resize((tile_w,tile_h),Image.Resampling.LANCZOS),(x,y))
                draw.text((x+12,y+tile_h+9),f'{ids.index(r["id"])+1:02d}  {r["title"]}',fill='#3e5144',font=font(15))
                draw.text((x+12,y+tile_h+31),r['id'],fill='#98a49d',font=font(11))
            name=f'contact-{prefix}-{n:02d}.jpg';sheet.save(out/name,quality=94,subsampling=0)
            sheets.append(dict(file=name,category=c,theme=theme))
    gallery=HTML.replace('__DATA__',json.dumps(rows,ensure_ascii=False).replace('</','<\\/')).replace('__SHEETS__',json.dumps(sheets,ensure_ascii=False))
    before=list(out.glob('before-*.png'))
    if before:
        (out/'changes.html').write_text(CHANGES,encoding='utf-8')
        gallery=gallery.replace('<h1>全界面审核</h1>','<h1>全界面审核</h1><p><a href="changes.html" style="color:#3f705e">查看本轮三处修改的前后对比 ↗</a></p>')
    (out/'index.html').write_text(gallery,encoding='utf-8')
    if (out/'grok.html').exists():
        gallery=gallery.replace('<h1>全界面审核</h1>','<h1>全界面审核</h1><p><a href="grok.html" style="color:#7764a3">Grok Ball · 32 种表情与动态审核 ↗</a></p>')
        (out/'index.html').write_text(gallery,encoding='utf-8')
    if (out/'mijia.html').exists():
        gallery=gallery.replace('<h1>全界面审核</h1>','<h1>全界面审核</h1><p><a href="mijia.html" style="color:#3486c5">米家卡片 · 59 个品类 / 127 种组合与更多操作 ↗</a></p>')
        (out/'index.html').write_text(gallery,encoding='utf-8')
    (out/'contact-sheets.json').write_text(json.dumps(sheets,ensure_ascii=False,indent=2),encoding='utf-8')
    info=dict(renderer='LVGL 9 / RGB565 framebuffer + real QuickJS LVGL UI bindings',resolution=[1024,600],
              states=len(ids),screenshots=len(rows),source='Existing native C secondary pages and embedded QPK resources',
              fixtures='Offline service data; no hardware or network calls; media workers disabled',
              limitations=['Camera and video contain no decoded device frames','Dictionary and voice backends are offline; IME layout is production C','App palettes are retained even when the desktop theme changes'],
              flashed=False,categories={c:len([r for r in rows if r['category']==c and r['theme']=='浅色']) for c in categories})
    (out/'review-info.json').write_text(json.dumps(info,ensure_ascii=False,indent=2),encoding='utf-8')
    if (out/'board-validation.json').exists():
        board=json.loads((out/'board-validation.json').read_text(encoding='utf-8'))
        info['flashed']=board['flashed'];info['board_test']=board
        (out/'review-info.json').write_text(json.dumps(info,ensure_ascii=False,indent=2),encoding='utf-8')
        gallery=gallery.replace('未连接硬件、未刷机。','这些截图为离线渲染；实板烧录和测试结果见下方报告。')
        gallery=gallery.replace('<h1>全界面审核</h1>','<h1>全界面审核</h1><p><a href="board-test.html" style="color:#3486c5">实板烧录与应用测试报告 ↗</a></p>')
        (out/'index.html').write_text(gallery,encoding='utf-8')
    note=f'LVGL 全界面审核\n\n{len(ids)} 个页面/状态，{len(rows)} 张原始 PNG，尺寸 1024×600。\n解压后双击 index.html，可以按功能筛选、切换主题、并排对比和放大原图。\n\n复用现有二级界面。数据为离线审核示例。相机/视频无实际硬件画面。\n输入法使用原有布局；词库和语音服务保持离线。独立应用保留原有配色。\n未连接设备，未刷机。\n'
    (out/'使用说明.txt').write_text(note,encoding='utf-8')
    files=['index.html','screens.json','review-info.json','contact-sheets.json','使用说明.txt']+[r['file'] for r in rows]+[s['file'] for s in sheets]
    if (out/'render-sources.json').exists():files.append('render-sources.json')
    if before:files+=['changes.html']+[p.name for p in before]
    if (out/'grok.html').exists():files+=['grok.html','grok-animations.json']+[p.name for p in out.glob('*-grok-*.gif')]
    if (out/'mijia.html').exists():files+=['mijia.html','mijia-catalog.json','mijia-render-sources.json']+[p.name for p in out.glob('mijia-card-*.png')]+[p.name for p in out.glob('mijia-reference-*.png')]
    if (out/'board-validation.json').exists():files+=['board-validation.json','board-test.html']
    with zipfile.ZipFile(out/'LVGL-全界面审核.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for name in files:archive.write(out/name,name)
    print(json.dumps(info,ensure_ascii=False));print('GALLERY_OK',out/'index.html')


if __name__=='__main__':main()
