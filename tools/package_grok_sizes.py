#!/usr/bin/env python3
"""Package tiny Grok Ball native previews; no firmware or app-entry mutations."""
from pathlib import Path
import argparse,hashlib,json,re,shutil
import numpy as np
from PIL import Image

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['native','candidate','output']:p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();repo=Path(__file__).resolve().parents[2]
    sources=json.loads((a.candidate/'SOURCES.json').read_text());native=json.loads((a.native/'native-performance.json').read_text())
    for path,value in sources['production_sha256'].items():assert sha(repo/path)==value,path
    variants={v['name']:v for v in sources['variants']};dest=a.output/'grok-sizes';dest.mkdir(exist_ok=True)
    animation_records=[]
    for name,kind in [('mini','previous'),('medium','after')]:
        assert variants[name]['app_sha256']==sha(a.candidate/name/'app.js')==native['results'][kind]['app_sha256']
        assert native['results'][kind]['polygon_sha256']==sha(a.candidate/'polygon.inc')
        folder=dest/name;folder.mkdir(exist_ok=True)
        paths=list((a.native/kind).glob('*.png'));assert len(paths)==50
        for path in paths:shutil.copy2(path,folder/path.name)
        for scenario in ['idle','thinking','spin','confetti']:
            paths=sorted((a.native/kind/'motion').glob(scenario+'-*.rgb565'));assert len(paths)==30
            frames=[]
            for path in paths:
                raw=np.frombuffer(path.read_bytes(),dtype='<u2').reshape(600,1024).astype(np.uint32)
                rgb=np.stack([((raw>>11)&31)*255//31,((raw>>5)&63)*255//63,(raw&31)*255//31],axis=2).astype('uint8')
                frames.append(Image.fromarray(rgb).quantize(colors=256))
            gif=folder/(scenario+'.gif')
            frames[0].save(gif,save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
            with Image.open(gif) as im:
                encoded=im.n_frames;duration=0
                for i in range(encoded):im.seek(i);duration+=im.info['duration']
            assert duration==3000
            animation_records.append({'variant':name,'scenario':scenario,'native_frames':30,'encoded_frames':encoded,'duration_ms':duration,'capture_interval_ms':100})
    records=[]
    labels={'idle':'待机','thinking':'思考彩带','spin':'自旋','confetti':'完成撒花'}
    for i in range(4):
        base=native['results']['before']['median_metrics'][i];total=lambda m:(m['js_ms']+m['draw_ms'])/60
        row={'scenario':base['scenario'],'name':labels[base['scenario']],'reference_ms_per_frame':total(base)}
        for name,kind in [('mini','previous'),('medium','after')]:
            m=native['results'][kind]['median_metrics'][i]
            row[name]={'ms_per_frame':total(m),'total_reduction_percent':100*(1-total(m)/total(base)),'draw_reduction_percent':100*(1-m['draw_ms']/base['draw_ms']),
                'script_ms_per_frame':m['js_ms']/60,'draw_ms_per_frame':m['draw_ms']/60,'js_heap_bytes':m['js_heap_bytes']}
        records.append(row)
    report={'flashed':False,'production_applied':False,'firmware_compiled':False,'board_tested':False,'sources':sources,
        'states_per_variant':32,'interaction_profiles_per_variant':8,'peak_widgets_per_variant':56,'partial_refresh_checks':96,
        'same_upstream_engine':True,'full_effects':True,'gradient_span_px':24,'tick_interval_ms':50,
        'animations':animation_records,'records':records,'native_measurements':native,
        'limits':'Desktop native QuickJS/LVGL with ASan/UBSan; three-run medians; not board FPS. Animation preview sampled every 100 ms and GIF palette encoded; PNG screenshots preserve the native RGB565 output. Shrinking does not reduce upstream particle count or JS geometry work.'}
    (a.candidate/'VALIDATION.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    shutil.copy2(a.candidate/'VALIDATION.json',dest/'validation.json')
    engine=(repo/'project/ouo/vendor/grok-ball.js').read_text(encoding='utf-8')
    states=re.findall(r"id: '([0-4][0-9])', name: '([^']+)'",engine);assert len(states)==32
    options=''.join(f'<option value="state-{code}">{code} · {name}</option>' for code,name in states)
    rows=''.join(f'<tr><th>{r["name"]}</th><td>{r["reference_ms_per_frame"]:.2f}</td><td>{r["mini"]["ms_per_frame"]:.2f}<small>少 {r["mini"]["total_reduction_percent"]:.1f}%</small></td><td>{r["medium"]["ms_per_frame"]:.2f}<small>少 {r["medium"]["total_reduction_percent"]:.1f}%</small></td></tr>' for r in records)
    document='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Grok Ball · 48 / 80 px 小尺寸审核</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f6f8;color:#27303a;font:15px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1120px;margin:32px auto;padding:0 24px}h1{font-size:30px;line-height:1.4;margin:14px 0}h2{font-size:21px;margin:0 0 12px}p{color:#78838e}.badge{display:inline-block;border-radius:24px;padding:5px 12px;background:#e6f2ed;color:#34765a;font-size:13px}a{color:#5a69a0}section{background:white;border:1px solid #e2e7ed;border-radius:20px;padding:20px;margin:20px 0}.toolbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:18px}button,select{font:inherit;border:1px solid #dce2e9;background:#fff;color:#65717c;padding:8px 15px;border-radius:10px;cursor:pointer}button.active{background:#e9e8f2;color:#655389;border-color:#ddd9eb}select{max-width:100%}.spacer{flex:1}.screen{display:block;max-width:1024px;width:100%;margin:auto;border-radius:12px;background:#fffbff}small{display:block;color:#8a96a2}.info{display:flex;justify-content:space-between;gap:12px;padding:10px 0 0}.note{font-size:13px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}.pair img{display:block;width:100%;border-radius:12px}figure{margin:0}figcaption{margin-top:8px;color:#758390;font-size:13px}table{width:100%;border-collapse:collapse;text-align:left}th,td{padding:10px 12px;border-bottom:1px solid #edf0f4}.scroll{overflow:auto}@media(max-width:640px){main{padding:0 12px;margin:20px auto}h1{font-size:24px}section{padding:14px}.pair{grid-template-columns:1fr}.info{flex-wrap:wrap}td,th{font-size:12px;padding:8px}}
</style><main><a href="grok-quality.html">画质分档对比 ↗</a><br><div class="badge">两版已缩小 · 待审核 · 未烧录</div><h1>迷你 48 px / 中型 80 px</h1><p>球体进一步缩小，状态文字和触摸范围同步调整。保留全部 32 种表情、彩带、撒花与交互。这一轮保持 24 px 渐变分段，没有叠加上一轮的渐变降质。</p>
<section><div class="toolbar"><button class="active" data-size="mini">迷你 · 48 px</button><button data-size="medium">中型 · 80 px</button><span class="spacer"></span><select id="scene" aria-label="选择动作或表情"><optgroup label="动态预览"><option value="thinking">思考 · 环绕彩带</option><option value="idle">待机</option><option value="spin">自旋</option><option value="confetti">撒花</option></optgroup><optgroup label="32 种表情 · 静态截图">OPTIONS</optgroup></select></div><a id="full" href="grok-sizes/mini/thinking.gif" target="_blank"><img class="screen" id="preview" src="grok-sizes/mini/thinking.gif" alt="迷你版原生 LVGL 动态预览"></a><div class="info"><b id="size-label">迷你版 · 球体直径约 48 px</b><a id="png" href="grok-sizes/mini/thinking-024.png" target="_blank">原生 PNG / 原尺寸 ↗</a></div><p class="note">预览使用完整 1024 × 600 界面，随页面等比例缩放；可打开原尺寸查看。动图来自原生连续帧，每 100 ms 采样一次，供外观审核，不代表实板帧率。</p></section>
<section><h2>同一帧，直接比较大小</h2><div class="pair"><figure><a href="grok-sizes/mini/thinking-024.png" target="_blank"><img src="grok-sizes/mini/thinking-024.png" alt="48 px 迷你版完整界面"></a><figcaption>迷你 · 48 px</figcaption></figure><figure><a href="grok-sizes/medium/thinking-024.png" target="_blank"><img src="grok-sizes/medium/thinking-024.png" alt="80 px 中型版完整界面"></a><figcaption>中型 · 80 px</figcaption></figure></div></section>
<section class="scroll"><h2>离线处理耗时 · ms/帧</h2><table><thead><tr><th>场景</th><th>原尺寸约 300 px</th><th>迷你 48 px</th><th>中型 80 px</th></tr></thead><tbody>ROWS</tbody></table><p class="note">同轮真实 QuickJS / LVGL，3 次测量取中位数。缩小主要节省绘制，脚本仍计算完整特效；迷你版不保证每种场景都比中型更快。尚未测开发板 FPS 或功耗。</p></section>
<section><h2>审核状态</h2><p>两版共 16 组主题/启动/坐标接口检查通过；原生渲染覆盖全部表情，三版本共 96 次局部刷新核对通过。正式应用入口和固件未替换，开发板未烧录。</p><a href="grok-sizes/validation.json">完整验证数据 ↗</a></section></main><script>
let size='mini';const scene=document.querySelector('#scene');function render(){const state=scene.value.startsWith('state-'),file=scene.value+(state?'.png':'.gif'),url='grok-sizes/'+size+'/'+file;document.querySelector('#preview').src=url;document.querySelector('#preview').alt=(size==='mini'?'48 px 迷你':'80 px 中型')+'原生预览';document.querySelector('#full').href=url;document.querySelector('#png').href='grok-sizes/'+size+'/'+(state?scene.value:scene.value+'-024')+'.png';document.querySelector('#size-label').textContent=(size==='mini'?'迷你版 · 球体直径约 48 px':'中型版 · 球体直径约 80 px');for(const b of document.querySelectorAll('[data-size]'))b.classList.toggle('active',b.dataset.size===size);}document.querySelectorAll('[data-size]').forEach(b=>b.onclick=()=>{size=b.dataset.size;render();});scene.onchange=render;
</script></html>'''
    (a.output/'grok-sizes.html').write_text(document.replace('OPTIONS',options).replace('ROWS',rows),encoding='utf-8')
    print('GROK_SIZES_REVIEW_OK',json.dumps(records,ensure_ascii=True))
if __name__=='__main__':main()
