#!/usr/bin/env python3
"""Run the production host/hub allocation code with ASan/UBSan."""
import argparse
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
out=a.output.resolve();(out/'nuttx').mkdir(parents=True,exist_ok=True)
(out/'nuttx/config.h').write_text('#pragma once\n#define __INCLUDE_NUTTX_CONFIG_H\n')
(out/'nuttx/kmalloc.h').write_text('#pragma once\n#include <stddef.h>\nvoid *test_pool_alloc(size_t);\n#define kumm_zalloc test_pool_alloc\n')
driver=repo/'app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/esp32p4/camera_usb'
cherry=driver/'cherryusb'
flags=['-std=gnu11','-D_GNU_SOURCE','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
       '-fno-pie','-no-pie','-ffunction-sections','-fdata-sections',
       *['-I'+str(path) for path in [out,driver,cherry/'common',cherry/'core',cherry/'class/hub']]]
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'}
for name,extra in [('bus',[]),('hub',['-DTEST_HUB_POOL'])]:
    exe=out/name
    subprocess.run(['gcc',*flags,*extra,str(repo/'app/espdl-quickapp/tests/usb_pool_host.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env=env)
exe=out/'lifecycle'
subprocess.run(['gcc',*flags,str(repo/'app/espdl-quickapp/tests/usb_hub_lifecycle_host.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,env=env)
