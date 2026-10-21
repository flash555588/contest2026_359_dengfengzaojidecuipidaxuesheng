#!/usr/bin/env python3
"""Exercise production camera-result persistence using an isolated filesystem."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];out=a.output.resolve();(out/'nuttx').mkdir(parents=True,exist_ok=True);(out/'nuttx/config.h').write_text('#pragma once\n')
desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop';cjson=a.apps/'netutils/cjson/cJSON'
with tempfile.TemporaryDirectory(prefix='camera-observation-') as folder:
 root=Path(folder)/'observations';exe=out/'camera-observation'
 subprocess.run(['gcc','-std=gnu11','-D_GNU_SOURCE','-DQPK_VOLUME_SD_ROOT="'+str(Path(folder)/'qpk')+'"','-DCAMERA_OBSERVATION_ROOT="'+str(root)+'"','-DCHAT_SD_ARCHIVE_ROOT="'+str(root)+'"',
  '-DCAMERA_RECORDING_ROOT="'+str(Path(folder)/'recordings')+'"',
  '-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O1','-g','-ffunction-sections','-fdata-sections','-pthread',
  '-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(out),'-I'+str(desktop),'-I'+str(cjson),
  str(repo/'app/espdl-quickapp/tests/camera_observation_host.c'),str(desktop/'glass_chat_store.c'),str(cjson/'cJSON.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
