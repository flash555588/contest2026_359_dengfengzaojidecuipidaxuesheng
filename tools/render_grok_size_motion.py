#!/usr/bin/env python3
"""Replay size candidates into native motion frames using the benchmark snapshot."""
from pathlib import Path
import argparse,os,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['native','apps','cache','desktop-source']:p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();native=a.native.resolve();apps=a.apps.resolve();cache=a.cache.resolve();desktop=a.desktop_source.resolve()
    qjs=apps/'interpreters/quickjs/quickjs';objects=sorted(cache.glob('lvgl-*.o'))+[(cache.parent/'all-ui-review')/(n+'.o') for n in ['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']]
    assert all(o.exists() for o in objects)
    for kind in ['previous','after']:
        target=native/kind;work=target/'source';out=target/'motion';out.mkdir(exist_ok=True)
        source=(work/'grok_performance.c').read_text()
        old='if(!lean&&(frame==0||frame==8||frame==24||frame==59))'
        assert source.count(old)==1
        source=source.replace(old,'if(!lean&&frame%2==0)').replace('if(lean)goto cleanup;','goto cleanup;')
        harness=out/'motion.c';harness.write_text(source)
        flags=['-std=gnu11','-O2','-g','-ffunction-sections','-fdata-sections','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DCONFIG_BIGNUM','-DLV_CONF_INCLUDE_SIMPLE']
        flags+=['-I'+str(x) for x in [work,desktop,apps/'system/desktop',apps/'graphics/lvgl',apps/'netutils/cjson/cJSON',cache,qjs]]
        flags+=['-DREVIEW_PRELUDE="'+str(work/'prelude.js')+'"']
        sources=[harness,work/'bridge.c',work/'service_mocks.c',desktop/'glass_icons.c',desktop/'glass_ime.c']
        command=['gcc',*flags,*map(str,sources),*map(str,objects),'-Wl,--gc-sections','-lm','-o',str(out/'render-motion')]
        with (out/'compile.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
        env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1'
        with (out/'native.log').open('w') as log:subprocess.run([str(out/'render-motion'),str(apps/'system/desktop/AlibabaPuHuiTi-3-55-Regular.otf'),str(work/'app.js'),str(out),kind],stdout=log,stderr=subprocess.STDOUT,env=env,check=True)
        assert len(list(out.glob('*.rgb565')))==120
        print('NATIVE_SIZE_MOTION_OK',kind,'120 frames; 100 ms capture interval',flush=True)
if __name__=='__main__':main()
