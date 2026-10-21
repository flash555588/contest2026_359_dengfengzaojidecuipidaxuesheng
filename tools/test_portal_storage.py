#!/usr/bin/env python3
"""Exercise Flash-authoritative portal files, no-op saves and SD independence."""
from pathlib import Path
import argparse,subprocess,tempfile,os
p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
a.output.mkdir(parents=True,exist_ok=True);(a.output/'nuttx').mkdir(exist_ok=True);(a.output/'nuttx/config.h').write_text('#pragma once\n')
with tempfile.TemporaryDirectory(prefix='portal-flash-') as folder:
 root=Path(folder);exe=a.output/'portal-storage'
 flags=['-DPORTAL_CONFIG_ROOT="'+str(root/'flash')+'"','-DPORTAL_TEST_SD_ROOT="'+str(root/'sd')+'"']
 subprocess.run(['gcc','-std=gnu11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O1','-g','-pthread',
  '-fsanitize=address,undefined','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections',*flags,
  '-I'+str(a.output),'-I'+str(desktop),'-I'+str(a.apps/'netutils/cjson/cJSON'),
  str(repo/'app/espdl-quickapp/tests/portal_storage_host.c'),str(desktop/'glass_agent_config.c'),
  str(a.apps/'netutils/cjson/cJSON/cJSON.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
