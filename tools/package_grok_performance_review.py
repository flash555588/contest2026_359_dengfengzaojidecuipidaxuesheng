#!/usr/bin/env python3
"""Package the measured offline Grok optimisation separately from board QA."""
from pathlib import Path
import argparse,datetime,hashlib,html,json,shutil

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();repo=Path(__file__).resolve().parents[1];workspace=repo.parent
    native=json.loads((a.native/'native-performance.json').read_text(encoding='utf-8'))
    bridge=json.loads((workspace/'output/grok-performance/bridge-workload.json').read_text(encoding='utf-8'))
    pending=workspace/'output/grok-performance/firmware'
    firmware=json.loads((pending/'BUILD-METADATA.json').read_text(encoding='utf-8'))
    assert not firmware['flashed'] and not firmware['board_tested']
    app_hash=hashlib.sha256((repo/'ouo/app.js').read_bytes()).hexdigest()
    assert native['results']['after']['app_sha256']==bridge['sourceSha256']['after']==app_hash
    assert native['results']['after']['polygon_sha256']==hashlib.sha256((repo/'app/espdl-quickapp/overlay/apps/system/desktop/qpk_polygon.inc').read_bytes()).hexdigest()
    for copy in [repo/'app/espdl-quickapp/overlay/apps/system/desktop/ouo/app.js',repo/'app/espdl-quickapp/overlay/apps/system/espclaw/qpk/ouo/app.js']:assert hashlib.sha256(copy.read_bytes()).hexdigest()==app_hash
    pin=json.loads((repo/'ouo/vendor/UPSTREAM.json').read_text(encoding='utf-8'))
    for name,digest in pin['source_sha256'].items():assert hashlib.sha256((repo/'ouo/vendor'/name).read_bytes()).hexdigest()==digest
    diverged=[]
    for source,digest in firmware['source_sha256'].items():
        snapshot=pending/'source-snapshot'/source
        compiled=snapshot if snapshot.exists() else repo/source
        assert hashlib.sha256(compiled.read_bytes()).hexdigest()==digest,source
        if hashlib.sha256((repo/source).read_bytes()).hexdigest()!=digest:diverged.append(source)
    for artifact,digest in firmware['artifact_sha256'].items():assert hashlib.sha256((pending/artifact).read_bytes()).hexdigest()==digest,artifact
    baseline=json.loads((workspace/'output/grok-performance/flashed-baseline-metadata.json').read_text(encoding='utf-8'))
    assert baseline['flashed'] and baseline['board_tested']
    metrics=[]
    names={'idle':'待机','gaze':'触摸注视','thinking':'思考彩带','spin':'自旋','confetti':'完成撒花','all-states':'32 种表情'}
    for before,after in zip(native['results']['before']['median_metrics'],native['results']['after']['median_metrics']):
        metrics.append({'scenario':before['scenario'],'name':names[before['scenario']],
            'draw_reduction_percent':100*(1-after['draw_ms']/before['draw_ms']),
            'total_reduction_percent':100*(1-(after['js_ms']+after['draw_ms'])/(before['js_ms']+before['draw_ms'])),
            'rect_reduction_percent':100*(1-after['rectangles']/before['rectangles']),
            'before_ms_per_frame':(before['js_ms']+before['draw_ms'])/before['frames'],
            'after_ms_per_frame':(after['js_ms']+after['draw_ms'])/after['frames'],
            'heap_added_kib':(after['js_heap_bytes']-before['js_heap_bytes'])/1024})
    iteration=[]
    if 'previous' in native['results']:
        prior=json.loads((workspace/'output/grok-performance/revision1/VALIDATION.json').read_text(encoding='utf-8'))
        assert native['results']['previous']['app_sha256']==prior['pending_source_sha256']
        for before,after in zip(native['results']['previous']['median_metrics'],native['results']['after']['median_metrics']):
            iteration.append({'name':names[before['scenario']],'js_reduction_percent':100*(1-after['js_ms']/before['js_ms']),
                'heap_saved_kib':(before['js_heap_bytes']-after['js_heap_bytes'])/1024,
                'before_js_ms':before['js_ms']/60,'after_js_ms':after['js_ms']/60,
                'total_reduction_percent':100*(1-(after['js_ms']+after['draw_ms'])/(before['js_ms']+before['draw_ms']))})
    summary={'created_at':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
        'status':'compiled; not flashed; awaiting user approval','device_firmware_sha256':baseline['artifact_sha256']['nuttx-refined-ui.bin'],
        'pending_firmware_sha256':firmware['artifact_sha256']['nuttx-refined-ui.bin'],'pending_source_sha256':app_hash,
        'post_build_workspace_changes':diverged,'compiled_source_snapshot':str(pending/'source-snapshot'),
        'bridge_workload':bridge,'native_performance':native,'summary_metrics':metrics,'iteration_metrics':iteration,
        'checks':{'grok_interaction_profiles':8,'states_per_profile':32,'bridge_geometry_frames_per_api':sum(r['frames'] for r in bridge['results']),'native_pixel_comparisons':len(native['pixel_comparisons'])+len(native.get('previous_pixel_comparisons',[])),'native_partial_refresh_comparisons':32*len(native['results']),'ha_tests':22},
        'expectation':'Reduced rendering work and fewer frame stalls; existing 50 ms timer (20 FPS ceiling). Device FPS/CPU/temperature/power have not been measured.',
        'precision_note':'Geometry and painter order match exactly in deterministic tests. Native RGB565 comparisons allow at most one channel quantisation step for float-gradient rounding.'}
    target=a.output/'grok-performance';target.mkdir(parents=True,exist_ok=True)
    for kind in native['results']:
        destination=target/kind;destination.mkdir(exist_ok=True)
        for path in (a.native/kind).glob('*.png'):shutil.copy2(path,destination/path.name)
        shutil.copy2(a.native/kind/'native.log',destination/'native.log')
        for path in (a.native/kind).glob('repeat-*.log'):shutil.copy2(path,destination/path.name)
    (target/'validation.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (workspace/'output/grok-performance/VALIDATION.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    rows=''.join(f'<tr><th>{m["name"]}</th><td>{m["before_ms_per_frame"]:.1f} → {m["after_ms_per_frame"]:.1f} ms</td><td>−{m["total_reduction_percent"]:.0f}%</td><td>−{m["rect_reduction_percent"]:.0f}%</td><td>+{m["heap_added_kib"]:.0f} KiB</td></tr>' for m in metrics)
    iteration_rows=''.join(f'<tr><th>{m["name"]}</th><td>{m["before_js_ms"]:.2f} → {m["after_js_ms"]:.2f} ms</td><td>−{m["js_reduction_percent"]:.1f}%</td><td>−{m["heap_saved_kib"]:.1f} KiB</td></tr>' for m in iteration)
    calls=''.join(f'<span>{names[r["scenario"]]} <b>−{r["bridgeCallReductionPercent"]:.0f}%</b></span>' for r in bridge['results'])
    shots=''.join(f'<article><h3>{name}</h3><div class="pair"><figure><img src="grok-performance/before/{file}.png"><figcaption>优化前</figcaption></figure><figure><img src="grok-performance/after/{file}.png"><figcaption>优化后 · 原生离线渲染</figcaption></figure></div></article>' for name,file in [('思考彩带','thinking-024'),('自旋彩带','spin-008'),('完成撒花','confetti-008'),('睡眠','state-00')])
    states=''.join(f'<a href="grok-performance/after/{path.name}"><img loading="lazy" src="grok-performance/after/{path.name}"><span>表情 {html.escape(path.stem[6:])}</span></a>' for path in sorted((a.native/'after').glob('state-*.png')))
    document='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>小球性能优化 · 待审核</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f6f8;color:#232a32;font:16px/1.65 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1180px;margin:48px auto;padding:0 24px}h1{font-size:34px;letter-spacing:-1px;margin:8px 0}h2{font-size:24px;margin-top:38px}h3{font-size:18px}p{color:#67727e}a{color:#586db3}.badge{display:inline-block;background:#fff1dc;color:#916923;padding:7px 14px;border-radius:30px}.summary,article{background:white;padding:24px;border:1px solid #e8ebee;border-radius:20px;margin:18px 0}.calls{display:flex;gap:12px;flex-wrap:wrap}.calls span{background:#f2f4f8;padding:10px 16px;border-radius:12px}.calls b{color:#2b8663;margin-left:10px}table{width:100%;border-collapse:collapse;text-align:left}td,th{padding:12px;border-bottom:1px solid #edf0f3}tbody th{font-weight:500}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0}img{display:block;width:100%;border-radius:12px}figcaption{color:#7c8793;font-size:14px;margin-top:8px}.states{display:grid;grid-template-columns:repeat(4,1fr);gap:14px}.states a{background:white;border-radius:12px;padding:8px;text-decoration:none}.states span{display:block;text-align:center;font-size:13px}code{overflow-wrap:anywhere;font-size:12px}.note{font-size:14px}@media(max-width:700px){main{margin:24px auto;padding:0 14px}.pair{grid-template-columns:1fr}.states{grid-template-columns:repeat(2,1fr)}table{font-size:12px}td,th{padding:8px}}</style><main>
<a href="board-test.html">上一版实板测试报告 ↗</a><br><div class="badge">第二轮已编译 · 未烧录 · 等待你同意</div><h1>小球性能优化</h1><p>本轮复用整数坐标缓冲区，减少小数组分配与逐点跨接口转换；缓存世界变换和撒花轮廓，修正控件编号复用时的层级重排。保留 32 种表情、彩带、撒花和原有交互。</p>
<h2>本轮相对上一轮</h2><p>脚本开销与场景结束时的 JS 堆均继续下降。原来的数组接口保留，旧平台可以自动回退。</p><section class="summary"><table><thead><tr><th>场景</th><th>脚本平均耗时</th><th>脚本耗时减少</th><th>JS 堆减少</th></tr></thead><tbody>ITERATION_ROWS</tbody></table><p class="note">本轮收益主要来自脚本与内存开销，总处理耗时改善随场景变化。实板效果尚未测试。</p></section>
<section class="summary"><h3>累计测得：比最初版本的界面调用减少 65%–76%</h3><div class="calls">CALLS</div><p class="note">固定随机种子，2,480 帧逐帧比较；数组与缓冲区各核对一轮。图形坐标、颜色参数、渐变参数和前后层级一致。</p></section>
<h2>当前版本与最初版本</h2><p>真实 QuickJS / LVGL，桌面 CPU，开启内存与未定义行为检查。每个场景 60 帧、重复 3 轮取中位数。数据用于比较开销，不能换算成实板帧率。</p><section class="summary"><table><thead><tr><th>场景</th><th>平均处理耗时</th><th>耗时减少</th><th>绘制请求减少</th><th>JS 堆变化</th></tr></thead><tbody>ROWS</tbody></table><p class="note">缓存用少量内存换取更少的绘制工作。脚本处理部分在复杂表情中略有增加，总处理时间下降。实板 CPU、功耗及温度尚未测试。</p></section>
<h2>预期表现</h2><p>复杂彩带和撒花时减少掉帧，触摸与返回更容易及时响应。保留现有 50 ms 动画节奏，目标仍为 20 FPS 上限；实板效果等待你授权后验证。</p>
<h2>外观核对</h2><p>原生渲染对比共 PIXELS 张；局部刷新核对 PARTIALS 次通过。渐变使用单精度后，RGB565 像素最多相差一个通道量化级。8 个明暗主题、启动方式与坐标接口组合均通过表情和交互测试；控件编号复用、缓冲区分离及参数转换检查通过。以下截图只展示应用内容，未绘制桌面标题栏。</p>SHOTS
<h2>32 种表情</h2><div class="states">STATES</div>
<section class="summary"><h3>待烧录产物</h3><p>开发板仍运行上一版固件。本次没有打开串口或执行烧录。</p><p class="note">SNAPSHOT_NOTE</p><p class="note">新固件 SHA-256<br><code>DIGEST</code></p><a href="grok-performance/validation.json">完整验证数据 ↗</a></section></main></html>'''
    document=document.replace('ITERATION_ROWS',iteration_rows).replace('CALLS',calls).replace('ROWS',rows).replace('SHOTS',shots).replace('STATES',states).replace('DIGEST',summary['pending_firmware_sha256']).replace('PIXELS',str(summary['checks']['native_pixel_comparisons'])).replace('PARTIALS',str(summary['checks']['native_partial_refresh_comparisons']))
    document=document.replace('SNAPSHOT_NOTE',f'待审核固件使用已经编译与验证的源码快照。编译后工作区另有 {len(diverged)} 个文件发生修改，这些后续修改已保留，尚未合入该固件。' if diverged else '已核对源文件和编译产物哈希。')
    (a.output/'grok-performance.html').write_text(document,encoding='utf-8')
    print('GROK_PERFORMANCE_REVIEW_OK',json.dumps(iteration,ensure_ascii=True))
if __name__=='__main__':main()
