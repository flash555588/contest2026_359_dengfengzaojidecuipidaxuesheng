#!/usr/bin/env python3
"""Real QuickJS/dual-slot draft tests across fresh processes and host volumes."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);p.add_argument('--quickjs-cache',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];d=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
q=a.apps/'interpreters/quickjs/quickjs';cj=a.apps/'netutils/cjson/cJSON'
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
for case in ['sd','no-sd']:
 with tempfile.TemporaryDirectory(prefix='draft-storage-') as tmp:
  root=Path(tmp);internal=str(root/'data/qpk');sd=str(root/'sdcard/qpk')
  flags=['gcc','-std=gnu11','-D_GNU_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-pie','-no-pie','-DCONFIG_BIGNUM',
    '-DQPK_BUILDER_ROOT="'+internal+'"','-DQPK_VOLUME_SD_ROOT="'+sd+'"','-I'+str(d),'-isystem',str(q),'-I'+str(cj)]
  sources=[repo/'app/espdl-quickapp/tests/draft_storage_host.c',d/'qpk_storage.c',d/'qpk_espdl_sha256.c',d/'glass_chat_store.c',cj/'cJSON.c']
  objects=[a.quickjs_cache/(n+'.o') for n in ['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']]
  exe=root/'test'
  subprocess.run([*flags,*map(str,sources),*map(str,objects),'-Wl,--gc-sections','-lm','-ldl','-pthread','-o',str(exe)],check=True)
  subprocess.run([str(exe),'seed' if case=='sd' else 'no-sd'],check=True,timeout=40,env=env)
  if case=='sd':
   subprocess.run([str(exe),'reload'],check=True,timeout=20,env=env)
   for slot in ['draft0.json','draft1.json']:(Path(sd)/'.draft'/slot).write_text('broken')
   subprocess.run([str(exe),'corrupt'],check=True,timeout=20,env=env)
