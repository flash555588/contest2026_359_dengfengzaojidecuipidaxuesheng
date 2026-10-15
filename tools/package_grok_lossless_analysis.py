#!/usr/bin/env python3
"""Publish an isolated lossless experiment without replacing firmware or apps."""
from pathlib import Path
import argparse,datetime,hashlib,json,shutil

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();native=json.loads((a.native/'native-performance.json').read_text(encoding='utf-8'))
    bridge=json.loads((a.candidate/'bridge-comparison.json').read_text(encoding='utf-8'))
    assert native['results']['after']['app_sha256']==sha(a.candidate/'candidate-app.js')==bridge['sourceSha256']['after']
    assert native['results']['after']['polygon_sha256']==sha(a.candidate/'candidate-polygon.inc')
    assert native['results']['before']['app_sha256']==sha(a.candidate/'current-app.js')==bridge['sourceSha256']['before']
    # Strictly lossless relative to the original double-precision port. Do not
    # accept the one-step tolerance used by earlier near-identical experiments.
    assert len(native['previous_pixel_comparisons'])==50
    assert all(r['changed_pixels']==0 and r['maximum_rgb565_channel_step']==0 for r in native['previous_pixel_comparisons'])
    repo=Path(__file__).resolve().parents[1]
    assert sha(repo/'ouo/app.js')==sha(a.candidate/'current-app.js'),'Production application changed during the isolated experiment'
    names={'idle':'待机','thinking':'思考彩带','spin':'自旋','confetti':'完成撒花'}
    records=[]
    for current,original,candidate in zip(native['results']['before']['median_metrics'],native['results']['previous']['median_metrics'],native['results']['after']['median_metrics']):
        total=lambda r:r['js_ms']+r['draw_ms']
        records.append({'scenario':candidate['scenario'],'name':names[candidate['scenario']],
            'original_ms_per_frame':total(original)/60,'current_ms_per_frame':total(current)/60,'candidate_ms_per_frame':total(candidate)/60,
            'reduction_vs_original_percent':100*(1-total(candidate)/total(original)),
            'reduction_vs_current_percent':100*(1-total(candidate)/total(current)),
            'script_reduction_vs_current_percent':100*(1-candidate['js_ms']/current['js_ms']),
            'additional_js_heap_kib':(candidate['js_heap_bytes']-current['js_heap_bytes'])/1024,
            'rectangles_original':original['rectangles'],'rectangles_candidate':candidate['rectangles']})
    report={'created_at':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
        'status':'isolated feasibility experiment; production app unchanged; no firmware build or flash',
        'flashed':False,'firmware_compiled':False,'board_tested':False,'production_applied':False,
        'lossless_reference':'original LVGL port before float-gradient optimisation; not upstream browser pixels',
        'pixel_identical_to_original':True,'pixel_comparisons':50,'partial_refresh_checks':96,
        'geometry_frames_per_api':2480,'interaction_profiles':8,'states_per_profile':32,
        'candidate_sha256':{'app':sha(a.candidate/'candidate-app.js'),'polygon':sha(a.candidate/'candidate-polygon.inc')},
        'records':records,'native_measurements':native,'bridge_validation':bridge,
        'remaining_candidates':[{'change':'SVG path parsing and string churn','status':'code-based hypothesis; profile before moving parsing into native code'},
            {'change':'exact gradient-field cache with a fixed memory cap','status':'possible CPU/memory tradeoff; measure hit rate during breathing/rotation first'}],
        'excluded_as_lossless':['lower update rate','fewer ribbon/confetti particles','fewer contour vertices','coarser gradient segments','approximate math or colour precision'],
        'limits':'Desktop LVGL/QuickJS with sanitizers; three runs. No device FPS, power or temperature measurement. Frame timings include script plus draw, not a physical refresh period.'}
    (a.candidate/'ANALYSIS.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    dest=a.output/'grok-lossless';dest.mkdir(parents=True,exist_ok=True)
    for kind in ['previous','before','after']:
        folder=dest/kind;folder.mkdir(exist_ok=True)
        for path in (a.native/kind).glob('*.png'):shutil.copy2(path,folder/path.name)
        for path in (a.native/kind).glob('*.log'):shutil.copy2(path,folder/path.name)
    shutil.copy2(a.candidate/'ANALYSIS.json',dest/'analysis.json')
    rows=''.join(f'<tr><th>{r["name"]}</th><td>{r["original_ms_per_frame"]:.2f}</td><td>{r["current_ms_per_frame"]:.2f}</td><td>{r["candidate_ms_per_frame"]:.2f}</td><td>−{r["reduction_vs_original_percent"]:.1f}%</td><td>+{r["additional_js_heap_kib"]:.1f} KiB</td></tr>' for r in records)
    pictures=''.join(f'<article><h3>{name}</h3><div class="pair"><figure><img src="grok-lossless/previous/{file}.png"><figcaption>原始双精度版本</figcaption></figure><figure><img src="grok-lossless/after/{file}.png"><figcaption>严格无损候选 · 像素一致</figcaption></figure></div></article>' for name,file in [('思考彩带','thinking-024'),('撒花','confetti-008'),('睡眠','state-00')])
    current_delta=sum(r['changed_pixels'] for r in native['pixel_comparisons'])
    document='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>小球无损优化分析</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f6f8;color:#232a32;font:16px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1180px;margin:48px auto;padding:0 24px}h1{font-size:34px;margin:10px 0}h2{font-size:23px;margin-top:32px}h3{font-size:18px}p{color:#66717d}a{color:#5368b0}.badge{display:inline-block;background:#e5f4ed;color:#287151;border-radius:30px;padding:7px 14px}.box,article{background:white;border:1px solid #e7ebef;border-radius:20px;padding:24px;margin:20px 0}.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:14px}.stats b{display:block;font-size:32px}.stats span{color:#78828c}table{width:100%;border-collapse:collapse;text-align:left}th,td{padding:12px 9px;border-bottom:1px solid #edf0f3}tbody th{font-weight:500}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0}img{width:100%;display:block;border-radius:12px}figcaption,.note{font-size:14px;color:#7a8590}.scroll{overflow:auto}@media(max-width:700px){main{margin:24px auto;padding:0 14px}.stats,.pair{grid-template-columns:1fr}.box{padding:16px}table{font-size:12px}th,td{padding:8px 5px}}</style><main>
<a href="grok-performance.html?rev=packed-points">第二轮性能审核 ↗</a><br><div class="badge">独立候选验证 · 未替换正式代码 · 未烧录</div><h1>还能无损降低开销吗？可以。</h1>
<p>保持同一动画时间序列、32 种表情、完整彩带与撒花。候选恢复原始双精度渐变，进一步复用渐变数据、绘制状态和参数记录，去掉重复计算与临时分配。</p>
<section class="box stats"><div><b>0</b><span>相对原始版本的变化像素</span></div><div><b>50 张</b><span>严格逐像素对比通过</span></div><div><b>96 次</b><span>局部刷新一致性核对</span></div></section>
<h2>什么算无损</h2><p>这里以最初 LVGL 移植版本为基准：帧的几何、颜色、透明度和层级相同；不降低更新频率、不删除特效、不减少顶点。8 个主题/启动/坐标接口组合，以及 2,480 帧的两种接口轨迹核对通过。</p><p class="note">当前第二轮版本包含之前的单精度近似；本次恢复双精度后，相对该版本共恢复 DELTA 个像素的量化差异。无损指原始 LVGL 输出一致，未声称与网页 SVG 渲染器逐像素一致。</p>
<h2>离线实测与代价</h2><p>真实 QuickJS / LVGL，开启内存和未定义行为检查。每场景 60 帧，3 轮取中位数，单位为 ms/帧。</p><section class="box scroll"><table><thead><tr><th>场景</th><th>最初版本</th><th>当前第二轮</th><th>严格无损候选</th><th>比最初减少</th><th>比当前多用 JS 堆</th></tr></thead><tbody>ROWS</tbody></table><p class="note">候选比当前版本的总处理时间变化随场景不同；自旋的微小差别接近测量噪声。缓存会增加几 KiB 的 JS 堆，不能称为所有资源都同时下降。设备帧率、CPU、功耗与温度尚未测量。</p></section>
<h2>下一步优先分析</h2><p>还值得继续查 SVG 路径字符串的反复解析，以及严格渐变色场的有限容量缓存。前者可减少脚本与垃圾回收开销，后者需要先测球体呼吸和旋转时的命中率。这两项仍是代码分析提出的方向，尚未计入性能收益。</p>
<h2>无损画面核对</h2>PICTURES
<section class="box"><h3>实验状态</h3><p>正式应用与待审核固件未被本实验替换。候选保存在独立目录，尚未编译为固件，更没有烧录。</p><a href="grok-lossless/analysis.json">查看完整测量、源码哈希与验证数据 ↗</a></section></main></html>'''
    document=document.replace('ROWS',rows).replace('PICTURES',pictures).replace('DELTA',str(current_delta))
    (a.output/'grok-lossless.html').write_text(document,encoding='utf-8')
    markdown=['# 小球严格无损优化可行性','', '结论：可以继续降低 CPU 开销，同时保持原始 LVGL 输出的像素一致。此轮只验证隔离候选，未替换正式代码、未编译固件、未烧录。','',
        '缓存和复用采用相同输入与相同算法；恢复原始双精度颜色计算，删除了候选中不再使用的单精度字段。保留动画节奏、全部表情、顶点与粒子数量。','',
        '| 场景 | 原始 ms/帧 | 当前 ms/帧 | 无损候选 ms/帧 | 比原始减少 | JS 堆增加（相对当前） |','|---|---:|---:|---:|---:|---:|']
    markdown += [f'| {r["name"]} | {r["original_ms_per_frame"]:.2f} | {r["current_ms_per_frame"]:.2f} | {r["candidate_ms_per_frame"]:.2f} | {r["reduction_vs_original_percent"]:.1f}% | {r["additional_js_heap_kib"]:.1f} KiB |' for r in records]
    markdown += ['', '数据来自桌面真实 QuickJS/LVGL，含 ASan/UBSan，3 轮中位数，不能换算为实板 FPS。自旋相对当前版本的微小差别可能是测量噪声。JS 堆取场景结束时的样本，不代表系统峰值；无损不是零内存代价。','',
        '相对原始双精度版本，50 张输出严格零像素变化，32 种表情与交互测试通过；三版本共 96 次局部刷新核对通过。几何/渐变参数/前后层级在数组与缓冲区两种接口各核对 2,480 帧。','',
        f'当前正式优化版使用过单精度近似，与严格候选的 50 张样本共存在 {current_delta} 个像素量化差异；候选恢复了原始输出。比较基准是原始 LVGL 移植版，并非上游浏览器 SVG。','',
        '后续优先测 SVG 路径解析与字符串分配，或有固定内存上限的精确色场缓存。二者目前只列为假设；先测适配层与引擎的耗时占比、以及呼吸/旋转时的缓存命中率，不承诺未测性能。','']
    (a.candidate/'ANALYSIS.md').write_text('\n'.join(markdown),encoding='utf-8')
    print('LOSSLESS_ANALYSIS_OK',json.dumps(records,ensure_ascii=True))
if __name__=='__main__':main()
