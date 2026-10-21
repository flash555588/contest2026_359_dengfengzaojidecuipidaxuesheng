#!/usr/bin/env python3
"""Check SD-only optional chat persistence with untouched legacy Flash files."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop';a.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='chat-sd-') as temporary:
 root=Path(temporary);(root/'legacy/chat').mkdir(parents=True)
 flags=['-DCHAT_DATA_ROOT="'+str(root/'legacy')+'"','-DCHAT_SD_HISTORY_ROOT="'+str(root/'sd/history')+'"',
        '-DCHAT_SD_ARCHIVE_ROOT="'+str(root/'sd/answers')+'"','-DQPK_VOLUME_SD_ROOT="'+str(root/'sd/qpk')+'"']
 exe=a.output/'chat-sd'
 subprocess.run(['gcc','-std=gnu11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
  '-O1','-g','-pthread','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-fno-pie','-no-pie',
  *flags,'-I'+str(desktop),'-I'+str(a.apps/'netutils/cjson/cJSON'),
  str(repo/'app/espdl-quickapp/tests/chat_sd_host.c'),str(desktop/'glass_chat_store.c'),
  str(a.apps/'netutils/cjson/cJSON/cJSON.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
