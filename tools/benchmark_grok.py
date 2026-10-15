#!/usr/bin/env python3
"""Reproducible offline QuickJS/LVGL before/after benchmark; never flash."""
from pathlib import Path
from array import array
import argparse,hashlib,json,os,re,shutil,subprocess,sys,statistics
from render_refined_home import png565

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apps',type=Path,required=True);p.add_argument('--cache',type=Path,required=True)
    p.add_argument('--before-app',type=Path,required=True);p.add_argument('--before-polygon',type=Path,required=True)
    p.add_argument('--previous-app',type=Path);p.add_argument('--previous-polygon',type=Path)
    p.add_argument('--desktop-source',type=Path,help='Replay an immutable compiled desktop snapshot instead of concurrent workspace changes')
    p.add_argument('--after-app',type=Path,help='Optional isolated candidate application')
    p.add_argument('--after-polygon',type=Path,help='Optional isolated candidate polygon renderer')
    p.add_argument('--harness-source',type=Path,help='Optional isolated regression harness, without modifying shared tests')
    p.add_argument('--allow-quality-loss',action='store_true',help='Explicit lossy experiment: record pixel differences without the default one-step limit; retain runtime and partial-refresh checks')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    repo=Path(__file__).resolve().parents[1];desktop=a.desktop_source.resolve() if a.desktop_source else repo/'app/espdl-quickapp/overlay/apps/system/desktop'
    apps=a.apps.resolve();cache=a.cache.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    qjs=apps/'interpreters/quickjs/quickjs';qjs_cache=cache.parent/'all-ui-review'
    results={}
    variants=[('before',a.before_app,a.before_polygon)]
    assert bool(a.previous_app)==bool(a.previous_polygon)
    if a.previous_app:variants.append(('previous',a.previous_app,a.previous_polygon))
    variants.append(('after',a.after_app or repo/'ouo/app.js',a.after_polygon or desktop/'qpk_polygon.inc'))
    for kind,app,polygon in variants:
        target=out/kind;target.mkdir(exist_ok=True)
        prepare=['python3',str(repo/'tools/render_all_ui.py'),'--apps',str(apps),'--cache',str(cache),'--output',str(target),'--prepare-only']
        if a.desktop_source:prepare+=['--desktop-source',str(desktop)]
        subprocess.run(prepare,check=True)
        work=target/'source';shutil.copy2(app,work/'app.js');shutil.copy2(polygon,work/'qpk_polygon.inc')
        # Count production draw-rect submissions without changing the drawing.
        path=work/'qpk_polygon.inc';text=path.read_text()
        if 'js_ui_polygon_buffer' not in text:
            registration=work/'production_qpk_registration.inc';registration.write_text('\n'.join(line for line in registration.read_text().splitlines() if 'polygonBuffer' not in line and 'polygonSetBuffer' not in line)+'\n')
        path.write_text('extern unsigned long long grok_test_rect_calls;\nstatic void grok_count_rect(lv_layer_t *l,const lv_draw_rect_dsc_t *d,const lv_area_t *a){grok_test_rect_calls++;lv_draw_rect(l,d,a);}\n#define lv_draw_rect grok_count_rect\n'+text+'\n#undef lv_draw_rect\n')
        prelude=work/'prelude.js';prelude.write_text((repo/'app/espdl-quickapp/tests/all_ui_fixture.js').read_text())
        bridge=work/'bridge.c';bridge_source=(repo/'app/espdl-quickapp/tests/qpk_review_bridge.c').read_text()
        # Replay the exact compiled runtime header even when another task has
        # subsequently extended the launch callback in the workspace tests.
        has_response_callback='qpk_unresponsive_cb_t' in (work/'qpk_runtime.h').read_text()
        if not has_response_callback:
            bridge_source=re.sub(r',\s*qpk_unresponsive_cb_t\s+unresponsive','',bridge_source)
            bridge_source=bridge_source.replace('(void)unresponsive;','')
        bridge_source=bridge_source.replace('static char review_error[256];','static JSValue grok_detach(JSContext *c,JSValueConst self,int argc,JSValueConst *argv){(void)self;if(argc)JS_DetachArrayBuffer(c,argv[0]);return JS_UNDEFINED;}\nstatic char review_error[256];')
        bridge_source=bridge_source.replace('JS_FreeValue(c,global);','JS_SetPropertyStr(c,global,"__testDetach",JS_NewCFunction(c,grok_detach,"__testDetach",1));JS_FreeValue(c,global);')
        bridge.write_text(bridge_source+'\nsize_t qpk_review_memory_usage(void){JSMemoryUsage m;JS_ComputeMemoryUsage(g_qpk.runtime,&m);return m.memory_used_size;}\n')
        flags=['-std=gnu11','-O2','-g','-ffunction-sections','-fdata-sections','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DCONFIG_BIGNUM','-DLV_CONF_INCLUDE_SIMPLE']
        flags += ['-I'+str(x) for x in [work,desktop,apps/'system/desktop',apps/'graphics/lvgl',apps/'netutils/cjson/cJSON',cache,qjs]]
        flags += ['-DREVIEW_PRELUDE="'+str(prelude)+'"']
        objects=sorted(cache.glob('lvgl-*.o'))+[qjs_cache/(name+'.o') for name in ['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']]
        assert all(x.exists() for x in objects),'Native cache is required'
        harness=work/'grok_performance.c';harness_source=(a.harness_source or repo/'app/espdl-quickapp/tests/grok_performance.c').read_text()
        if not has_response_callback:harness_source=harness_source.replace('font,message,message,NULL)==0','font,message,message)==0')
        elif 'font,message,message,NULL)==0' not in harness_source:harness_source=harness_source.replace('font,message,message)==0','font,message,message,NULL)==0')
        harness.write_text(harness_source)
        sources=[harness,bridge,work/'service_mocks.c',desktop/'glass_icons.c',desktop/'glass_ime.c']
        command=['gcc',*flags,*map(str,sources),*map(str,objects),'-Wl,--gc-sections','-lm','-o',str(target/'grok-benchmark')]
        with (target/'compile.log').open('w') as log:built=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
        if built.returncode:print((target/'compile.log').read_text());raise SystemExit(built.returncode)
        environment=os.environ.copy();environment['ASAN_OPTIONS']='detect_leaks=1'
        measured=subprocess.run([str(target/'grok-benchmark'),str(apps/'system/desktop/AlibabaPuHuiTi-3-55-Regular.otf'),str(work/'app.js'),str(target),kind],text=True,capture_output=True,env=environment)
        (target/'native.log').write_text(measured.stdout+measured.stderr)
        if measured.returncode:print(measured.stdout+measured.stderr);raise SystemExit(measured.returncode)
        metrics=[json.loads(m) for m in re.findall(r'(?m)^METRIC (.*)$',measured.stdout)]
        assert len(metrics)==4 and 'NATIVE_GROK_OK' in measured.stdout
        results[kind]={'app_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'polygon_sha256':hashlib.sha256(polygon.read_bytes()).hexdigest(),'metrics':metrics}
        for image in target.glob('*.rgb565'):png565(image)
        print(kind,json.dumps(metrics),flush=True)
    # Repeat only the timed animations in alternating before/after pairs.
    # Use medians; sanitizers and a desktop scheduler do not predict LCD FPS.
    for repetition in range(2):
        for kind,_,_ in variants:
            target=out/kind;work=target/'source'
            measured=subprocess.run([str(target/'grok-benchmark'),str(apps/'system/desktop/AlibabaPuHuiTi-3-55-Regular.otf'),str(work/'app.js'),str(target),kind+'-lean'],text=True,capture_output=True,env=environment)
            (target/f'repeat-{repetition+1}.log').write_text(measured.stdout+measured.stderr)
            if measured.returncode:print(measured.stdout+measured.stderr);raise SystemExit(measured.returncode)
            metrics=[json.loads(m) for m in re.findall(r'(?m)^METRIC (.*)$',measured.stdout)];assert len(metrics)==4
            results[kind].setdefault('repeated_metrics',[]).append(metrics)
    for kind,_,_ in variants:
        records=results[kind]
        records['median_metrics']=[dict(records['metrics'][i],**{key:statistics.median([records['metrics'][i][key]]+[r[i][key] for r in records['repeated_metrics']]) for key in ['js_ms','draw_ms','max_frame_ms']}) for i in range(4)]
    # RGB565 fields are compared separately; a single 8-bit rounding step can
    # cross a 565 quantisation boundary without indicating a geometry change.
    comparisons=[]
    for left in sorted((out/'before').glob('*.rgb565')):
        right=out/'after'/left.name
        x=array('H',left.read_bytes());y=array('H',right.read_bytes())
        if sys.byteorder!='little':x.byteswap();y.byteswap()
        assert len(x)==len(y)==1024*600
        changed=0;maximum=0
        for u,v in zip(x,y):
            if u!=v:
                changed+=1;maximum=max(maximum,abs((u>>11)-(v>>11)),abs(((u>>5)&63)-((v>>5)&63)),abs((u&31)-(v&31)))
        record={'file':left.name,'changed_pixels':changed,'maximum_rgb565_channel_step':maximum}
        if not a.allow_quality_loss:assert record['maximum_rgb565_channel_step']<=1,record
        comparisons.append(record)
    assert len(comparisons)==50
    previous_comparisons=[]
    if a.previous_app:
        for record in comparisons:
            left=out/'previous'/record['file'];right=out/'after'/record['file']
            x=array('H',left.read_bytes());y=array('H',right.read_bytes())
            if sys.byteorder!='little':x.byteswap();y.byteswap()
            assert len(x)==len(y)==1024*600
            changed=0;maximum=0
            for u,v in zip(x,y):
                if u!=v:changed+=1;maximum=max(maximum,abs((u>>11)-(v>>11)),abs(((u>>5)&63)-((v>>5)&63)),abs((u&31)-(v&31)))
            item={'file':left.name,'changed_pixels':changed,'maximum_rgb565_channel_step':maximum}
            if not a.allow_quality_loss:assert maximum<=1,item
            previous_comparisons.append(item)
    report={'kind':'offline native LVGL/QuickJS under ASan/UBSan; three runs; not board FPS','allow_quality_loss':a.allow_quality_loss,'flashed':False,'board_tested':False,'results':results,'pixel_comparisons':comparisons,'previous_pixel_comparisons':previous_comparisons,'partial_refresh_checks_per_variant':32}
    (out/'native-performance.json').write_text(json.dumps(report,indent=2)+'\n')
    print('GROK_NATIVE_PERFORMANCE_OK',len(comparisons),'pixel comparisons')
if __name__=='__main__':main()
