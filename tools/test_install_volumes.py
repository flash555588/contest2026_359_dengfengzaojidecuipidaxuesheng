#!/usr/bin/env python3
"""Run real installers, draft compiler and data cleanup on temporary host volumes."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);p.add_argument('--quickjs-cache',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop';q=a.apps/'interpreters/quickjs/quickjs'
with tempfile.TemporaryDirectory(prefix='install-volumes-') as tmp:
 root=Path(tmp);internal=str(root/'data');sd=str(root/'sdcard/qpk')
 cmd=['cc','-std=gnu11','-D_GNU_SOURCE','-O1','-g','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DCONFIG_BIGNUM',
      '-DQPK_BUILDER_ROOT="'+internal+'/qpk"','-DPORTAL_DATA_ROOT="'+internal+'"','-DQPK_VOLUME_SD_ROOT="'+sd+'"',
      '-I'+str(desktop),'-I'+str(q),'-I'+str(a.apps/'netutils/cjson/cJSON')]
 sources=[repo/'app/espdl-quickapp/tests/install_volumes_host.c',desktop/'qpk_storage.c',desktop/'qpk_espdl_sha256.c',desktop/'glass_chat_store.c',a.apps/'netutils/cjson/cJSON/cJSON.c']
 objects=[a.quickjs_cache/(n+'.o') for n in ['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']]
 subprocess.run([*cmd,*map(str,sources),*map(str,objects),'-Wl,--gc-sections','-lm','-ldl','-pthread','-o',str(root/'test')],check=True)
 subprocess.run([str(root/'test')],check=True,timeout=40)
