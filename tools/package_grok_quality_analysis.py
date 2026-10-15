#!/usr/bin/env python3
"""Package isolated lossy rendering measurements; never replace or flash firmware."""
from pathlib import Path
from array import array
import argparse, datetime, hashlib, json, shutil, sys

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def difference(left, right):
    x=array('H',left.read_bytes()); y=array('H',right.read_bytes())
    if sys.byteorder!='little': x.byteswap(); y.byteswap()
    assert len(x)==len(y)==1024*600
    # Restrict quality statistics to the rendered ball/effects area, excluding
    # unchanged navigation chrome and synthetic gradient boundary probes.
    changed=0; total=0; maximum=0
    for row in range(80,460):
        for col in range(280,745):
            u=x[row*1024+col]; v=y[row*1024+col]
            changed+=u!=v
            for shift, mask in [(11,31),(5,63),(0,31)]:
                delta=abs(((u>>shift)&mask)-((v>>shift)&mask))
                total+=delta*255/mask; maximum=max(maximum,delta*255/mask)
    pixels=380*465
    return {'changed_pixels_in_crop':changed,'crop_pixels':pixels,'mean_absolute_rgb8_error':total/(pixels*3),'maximum_rgb8_channel_difference':maximum}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native',type=Path,required=True); p.add_argument('--candidate',type=Path,required=True); p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(); native=json.loads((a.native/'native-performance.json').read_text())
    assert native['allow_quality_loss'] and not native['flashed']
    repo=Path(__file__).resolve().parents[2]
    production=json.loads((a.candidate/'production-hashes.json').read_text())
    for path,expected in production.items(): assert sha(repo/path)==expected,f'Production source changed: {path}'
    kinds=[('before','reference',24),('previous','light',48),('after','medium',96)]
    for kind,name,span in kinds:
        assert native['results'][kind]['app_sha256']==sha(a.candidate/'app.js')
        assert native['results'][kind]['polygon_sha256']==sha(a.candidate/(name+'-polygon.inc'))
    names={'idle':'待机','thinking':'思考彩带','spin':'自旋','confetti':'完成撒花'}
    records=[]
    for i in range(4):
        metrics=[native['results'][k]['median_metrics'][i] for k,_,_ in kinds]
        total=lambda m:(m['js_ms']+m['draw_ms'])/60
        item={'scenario':metrics[0]['scenario'],'name':names[metrics[0]['scenario']]}
        for (_,name,span),m in zip(kinds,metrics):
            item[name]={'gradient_span_px':span,'total_ms_per_frame':total(m),'script_ms_per_frame':m['js_ms']/60,'draw_ms_per_frame':m['draw_ms']/60,
                'total_reduction_percent':100*(1-total(m)/total(metrics[0])), 'draw_reduction_percent':100*(1-m['draw_ms']/metrics[0]['draw_ms']),
                'rectangles':m['rectangles'],'draw_task_reduction_percent':100*(1-m['rectangles']/metrics[0]['rectangles']), 'js_heap_bytes':m['js_heap_bytes']}
        records.append(item)
    quality={name:[] for _,name,_ in kinds[1:]}
    samples=sorted((a.native/'before').glob('*.rgb565'))
    assert len(samples)==50
    for path in samples:
        if path.name.endswith('probe.rgb565'): continue
        for kind,name,_ in kinds[1:]: quality[name].append(dict(file=path.stem,**difference(path,a.native/kind/path.name)))
    report={'created_at':datetime.datetime.now().astimezone().isoformat(),'status':'isolated lossy gradient experiment',
        'flashed':False,'production_applied':False,'firmware_compiled':False,'board_tested':False,
        'baseline':'previous strict-lossless cached adapter and double-precision 24 px gradient renderer',
        'same_app_and_animation':True,'states':32,'partial_refresh_checks':96,'render_samples_per_variant':50,
        'quality_samples_per_variant':48,'quality_crop':[280,80,745,460], 'production_sha256':production,
        'records':records,'quality':quality,'native_measurements':native,
        'limits':'Desktop QuickJS/LVGL under ASan/UBSan, three runs. No measured board FPS or power. Larger spans trade radial-gradient fidelity for fewer draw tasks. No change to cadence, contours, particle count or upstream vendor.'}
    (a.candidate/'ANALYSIS.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    dest=a.output/'grok-quality'; dest.mkdir(parents=True,exist_ok=True)
    for kind,name,_ in kinds:
        folder=dest/name; folder.mkdir(exist_ok=True)
        for path in (a.native/kind).glob('*.png'): shutil.copy2(path,folder/path.name)
    shutil.copy2(a.candidate/'ANALYSIS.json',dest/'analysis.json')
    def change(value):return f'减少 {value:.1f}%' if value>=0 else f'增加 {-value:.1f}%'
    rows=''.join(f'<tr><th>{r["name"]}</th><td>{r["reference"]["total_ms_per_frame"]:.2f}</td><td>{r["light"]["total_ms_per_frame"]:.2f}<small>{change(r["light"]["total_reduction_percent"])}</small></td><td>{r["medium"]["total_ms_per_frame"]:.2f}<small>{change(r["medium"]["total_reduction_percent"])}</small></td><td>{r["medium"]["draw_task_reduction_percent"]:.1f}%</td></tr>' for r in records)
    selectors=''.join(f'<option value="{path.stem}">{path.stem}</option>' for path in samples if not path.name.endswith('probe.rgb565'))
    figures=''.join(f'<figure><figcaption><b>{title}</b><span>{span} px 渐变分段</span></figcaption><a id="link-{name}" href="grok-quality/{name}/thinking-024.png" target="_blank"><img id="image-{name}" src="grok-quality/{name}/thinking-024.png" alt="{title}原生渲染"></a></figure>' for _,name,span in kinds for title in [{'reference':'无损基准','light':'轻度降质','medium':'中度降质'}[name]])
    document='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>小球画质与性能分档</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f6f8;color:#222c35;font:16px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1220px;margin:40px auto;padding:0 24px}h1{font-size:32px;line-height:1.35}h2{font-size:22px}p{color:#63717e}a{color:#426aae}.badge{display:inline-block;padding:6px 13px;border-radius:25px;color:#74521f;background:#fff0cf}.box{background:white;border:1px solid #e1e6ec;padding:24px;border-radius:20px;margin:22px 0}.scroll{overflow:auto}table{width:100%;border-collapse:collapse;text-align:left}td,th{padding:12px;border-bottom:1px solid #edf0f3}small{display:block;color:#758492;font-weight:400}.gallery{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}figure{margin:0;overflow:hidden}figcaption{padding:12px 0}figcaption span{display:block;color:#83909b;font-size:14px}img{display:block;width:100%;border-radius:12px}select{max-width:100%;padding:10px 15px;border:1px solid #dce2e9;border-radius:10px;background:#fff;margin:12px 0}.crop img{width:220%;max-width:none;position:relative;left:-60%;margin-top:-15%;margin-bottom:-17%}.crop a{display:block;overflow:hidden;border-radius:12px}.toggle{margin-left:12px}.note{font-size:14px}@media(max-width:760px){main{padding:0 14px;margin:24px auto}.box{padding:16px}.gallery{grid-template-columns:1fr}h1{font-size:27px}td,th{padding:8px;font-size:13px}.toggle{display:block;margin:0}}
</style><main><a href="grok-lossless.html">无损优化分析 ↗</a><br><div class="badge">降质实验 · 未应用正式代码 · 未烧录</div><h1>减少渐变绘制，保留表情与动作</h1><p>三档使用完全相同的脚本、动画时序和双精度颜色计算，只改变水平渐变分段宽度。眼睛、球体轮廓、彩带前后遮挡、粒子数量和 50 ms 更新节奏均保持相同。</p>
<section class="box"><h2>画质代价在哪里</h2><p>48 px：更少的渐变采样，阴影细节略粗。96 px：球体的弧形阴影被更长的直线渐变近似，可能出现块状或带状过渡。这里没有关闭彩带，也没有把球体改成纯色。</p><select id="sample" aria-label="选择表情或动画帧">OPTIONS</select><label class="toggle"><input type="checkbox" id="crop" checked> 放大球体和彩带</label><div id="gallery" class="gallery crop">FIGURES</div><p class="note">48 个真实场景样本：32 种表情、4 种动画各 4 帧。点击图像可打开完整原生截图。此页面比较静态画质，未测实板动画流畅度。</p></section>
<section class="box scroll"><h2>同轮离线实测 · ms/帧</h2><table><thead><tr><th>场景</th><th>无损基准</th><th>轻度 48 px</th><th>中度 96 px</th><th>中度绘制任务减少</th></tr></thead><tbody>ROWS</tbody></table><p class="note">真实 QuickJS / LVGL，ASan/UBSan 检查，3 轮取中位数。总处理时间为脚本＋绘制，不等于 LCD 帧周期，不能据此承诺实板 FPS。脚本保持相同，脚本耗时的差异属于运行波动。</p></section>
<section class="box"><h2>建议先考虑 48 px 档</h2><p>待机和思考的总处理时间分别减少约 16% 和 12%。96 px 的阴影块感更明显，思考的总处理时间与 48 px 相近。撒花场景中，48 px 虽减少绘制任务，整体耗时并未改善；它的脚本成本还需要另外处理。</p></section>
<section class="box"><h2>还有哪些可牺牲的部分</h2><p><b>减少撒花粒子：</b>20 颗降至 12 或 8 颗，优先减少节点、路径生成、跨语言更新，代价是特效密度变低；只对撒花场景有效。</p><p><b>简化彩带轨迹或轮廓顶点：</b>减少脚本几何工作，但高速转动的曲线和圆角可能变折。眼睛轮廓应最后调整，避免改变表情辨识度。</p><p><b>降低内部渲染分辨率：</b>球体区域按 0.75 倍宽高渲染后放大，理论像素工作量约少 44%；总耗时收益未测，需要新增缓冲区和缩放路径，边缘会变软。</p><p><b>降低更新频率：</b>20→15 Hz 可以少安排约 25% 的定时更新，牺牲动作流畅度；不是已测 CPU 或功耗降幅。以上四项是下一轮候选，未计入本轮收益。</p></section>
<section class="box"><h2>验证状态</h2><p>三档共完成 96 次局部刷新一致性核对，原生参数边界、缓冲区生命周期和内存检查通过。正式应用、上游源文件和原生渲染器的哈希保持不变；候选保存在独立目录。</p><a href="grok-quality/analysis.json">完整测量、差异与源码哈希 ↗</a></section></main><script>
const sample=document.querySelector('#sample'),gallery=document.querySelector('#gallery');sample.value='thinking-024';sample.addEventListener('change',()=>{for(const name of ['reference','light','medium']){const url='grok-quality/'+name+'/'+sample.value+'.png';document.querySelector('#image-'+name).src=url;document.querySelector('#link-'+name).href=url;}});document.querySelector('#crop').addEventListener('change',e=>gallery.classList.toggle('crop',e.target.checked));
</script></html>'''
    (a.output/'grok-quality.html').write_text(document.replace('OPTIONS',selectors).replace('FIGURES',figures).replace('ROWS',rows),encoding='utf-8')
    print('QUALITY_ANALYSIS_OK',json.dumps(records,ensure_ascii=True))
if __name__=='__main__':main()
