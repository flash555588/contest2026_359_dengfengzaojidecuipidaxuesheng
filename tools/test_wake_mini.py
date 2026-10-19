#!/usr/bin/env python3
"""Exercise the production wake overlay with native LVGL and real mini assets."""
import argparse,os,subprocess,tempfile
import re
from pathlib import Path
from render_refined_home import png565
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--harness-source',type=Path,help='Run a focused mini-flow harness')
for name in ['apps','cache','assets','output']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
a.output.mkdir(parents=True,exist_ok=True)
temporary=tempfile.TemporaryDirectory(prefix='wake-mini-');assets=Path(temporary.name)/'grok';assets.symlink_to(a.assets.resolve(),target_is_directory=True)
config=(a.cache/'lv_conf.h').read_text();config=re.sub(r'(#define\s+LV_USE_SNAPSHOT)\s+0',r'\1 1',config)
(Path(temporary.name)/'lv_conf.h').write_text(config)
cmd=['gcc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DLV_CONF_INCLUDE_SIMPLE','-DGROK_HOST_TEST','-DGROK_ASSET_ROOT="'+str(assets)+'"']
cmd+=['-I'+str(path) for path in [Path(temporary.name),desktop,a.apps/'graphics/lvgl',a.cache]]
cmd += [str(a.harness_source or repo/'app/espdl-quickapp/tests/wake_mini_host.c'),str(desktop/'glass_grok.c'),str(desktop/'glass_grok_state.c'),str(a.apps/'graphics/lvgl/lvgl/src/others/snapshot/lv_snapshot.c'),*map(str,sorted(a.cache.glob('lvgl-*.o'))),'-lm','-o',str(a.output/'wake-mini')]
with (a.output/'compile.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
with (a.output/'native.log').open('w') as log:subprocess.run([str(a.output/'wake-mini'),str(a.apps/'system/desktop/AlibabaPuHuiTi-3-55-Regular.otf'),str(a.output)],stdout=log,stderr=subprocess.STDOUT,check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
for path in a.output.glob('*.rgb565'):png565(path)
print((a.output/'native.log').read_text())
