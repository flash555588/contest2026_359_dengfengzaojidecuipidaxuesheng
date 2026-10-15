#!/usr/bin/env python3
"""Offline native pixel renderer; produces uncompressed RGB565+A8 SD banks."""
import argparse,hashlib,json,os,struct,subprocess
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['apps','cache','output']:p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--snapshot',type=Path,default=Path(__file__).resolve().parent/'grok_offline')
    a=p.parse_args();repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
    a.output.mkdir(parents=True,exist_ok=True);raw=a.output/'raw';raw.mkdir(exist_ok=True)
    # Preserve the final approved procedural implementation solely as a build tool.
    (a.output/'offline_grok.c').write_bytes((a.snapshot/'glass_grok.c').read_bytes())
    resource=(a.snapshot/'glass_grok_resource.c').read_text().replace("homeStyle===2||homeStyle===4","homeStyle===2||homeStyle===4||homeStyle===6").replace('return {init:init','return {ball:function(){return ball;},init:init')
    (a.output/'offline_resource.c').write_text(resource)
    qjs=a.apps/'interpreters/quickjs/quickjs'
    objects=sorted(a.cache.glob('lvgl-*.o'))+[a.cache.parent/'all-ui-review'/(n+'.o') for n in ['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']]
    cmd=['gcc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DCONFIG_BIGNUM','-DLV_CONF_INCLUDE_SIMPLE','-DGROK_HOST_TEST']
    cmd+=['-I'+str(x) for x in [a.output,desktop,qjs,a.apps/'graphics/lvgl',a.cache]]
    cmd+=[str(repo/'app/espdl-quickapp/tests/render_grok_sd.c'),str(a.output/'offline_resource.c'),str(a.snapshot/'ouo_resource.c'),str(desktop/'glass_grok_state.c'),*map(str,objects),'-lm','-o',str(a.output/'render')]
    with (a.output/'compile.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
    def render(v):
        last=51 if v>=3 else 41
        if (raw/f'v{v}-{last:02d}.bgra').exists():return
        with (a.output/f'render-{v}.log').open('w') as f:
            subprocess.run([str(a.output/'render'),str(raw),str(v)],stdout=f,stderr=subprocess.STDOUT,check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
    with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(render,range(5)))
    ids=list(range(8))+list(range(10,22))+list(range(30,42));manifest=[]
    names=['home-white','home-black','mini-white','app-white','app-black']
    banks=a.output/'sdcard';banks.mkdir(exist_ok=True)
    for v,name in enumerate(names):
        side=64 if v==2 else 128;frame_bytes=side*side*3;frames=80
        bankids=ids+list(range(42,52)) if v>=3 else ids
        # Header 32 bytes, 32 entries of {ID, frame count, frame offset} and
        # fixed-size, fully decoded, planar RGB565 + alpha frames. No decompressor.
        path=banks/(name+'.gbs');offset=32+len(bankids)*12
        with path.open('wb') as f:
            f.write(struct.pack('<8s6I',b'GROKSD1\0',1,side,side,100,len(bankids),frame_bytes))
            for i,id in enumerate(bankids):f.write(struct.pack('<3I',id,frames,offset+i*frames*frame_bytes))
            for id in bankids:
                rawpath=raw/f'v{v}-{id:02d}.bgra'
                data=rawpath.read_bytes();assert len(data)==frames*frame_bytes
                f.write(data)
        manifest.append(dict(file=path.name,bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),width=side,height=side,frames_per_state=frames,period_ms=100,states=bankids))
        print(name,path.stat().st_size,flush=True)
    (banks/'manifest.json').write_text(json.dumps({'format':'RGB565A8 raw / GROKSD1','source_sdk_sha256':hashlib.sha256((repo/'ouo/vendor/grok-ball.js').read_bytes()).hexdigest(),'banks':manifest},indent=2))
    print('GROK_SD_ASSETS_OK',flush=True)
if __name__=='__main__':main()
