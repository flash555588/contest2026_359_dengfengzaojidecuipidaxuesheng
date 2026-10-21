#!/usr/bin/env python3
"""Exercise production bounded UVC capture against deterministic transport faults."""
import argparse, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];out=a.output.resolve();(out/'nuttx').mkdir(parents=True,exist_ok=True)
(out/'nuttx/config.h').write_text('#pragma once\n')
overlay=repo/'app/espdl-quickapp/overlay';exe=out/'camera-clip'
subprocess.run(['gcc','-std=gnu11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O1','-g',
 '-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(out),'-I'+str(overlay/'nuttx/include'),
 '-I'+str(overlay/'apps/system/desktop'),str(repo/'app/espdl-quickapp/tests/camera_clip_host.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
