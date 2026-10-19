#!/usr/bin/env python3
"""Run native local-assistant regression with real storage, ASan and UBSan."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
a.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='assistant-service-') as temporary:
    root=Path(temporary)
    flags=['-D_GNU_SOURCE','-DASSISTANT_MANUAL_TICK','-std=gnu11','-O1','-g','-pthread',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections',
           '-I'+str(desktop),'-I'+str(a.apps/'netutils/cjson/cJSON'),
           '-DASSISTANT_DATA_ROOT="'+str(root/'data')+'"']
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'}
    commands=[['gcc',*flags,'-Dqpk_storage_write_sync=test_storage_write_sync_impl','-c',str(desktop/'qpk_storage.c'),'-o',str(root/'storage.o')],
              ['gcc',*flags,str(repo/'app/espdl-quickapp/tests/assistant_service_host.c'),str(desktop/'glass_agent_config.c'),
               str(desktop/'glass_chat_store.c'),str(a.apps/'netutils/cjson/cJSON/cJSON.c'),str(root/'storage.o'),
               '-Wl,--gc-sections','-lm','-o',str(root/'test')],
              [str(root/'test')],[str(root/'test'),'--restart'],
              ['gcc',*[flag for flag in flags if flag!='-DASSISTANT_MANUAL_TICK'],
               str(repo/'app/espdl-quickapp/tests/assistant_scheduler_thread_host.c'),str(desktop/'glass_agent_config.c'),
               str(desktop/'glass_chat_store.c'),str(a.apps/'netutils/cjson/cJSON/cJSON.c'),str(root/'storage.o'),
               '-Wl,--gc-sections','-lm','-o',str(root/'thread-test')],
              [str(root/'thread-test')]]
    logs=[]
    for command in commands:
        run=subprocess.run(command,capture_output=True,text=True,env=env)
        logs.append(run.stdout+run.stderr)
        (a.output/'test.log').write_text(''.join(logs),encoding='utf-8')
        if run.returncode:
            print(logs[-1]);raise SystemExit(run.returncode)
    (a.output/'verification.json').write_text(json.dumps(dict(passed=True,scope='Real local service, JSON, durable storage and scheduler; device/action boundaries are fixtures. Restart uses a fresh process.',markers=[line for line in ''.join(logs).splitlines() if '_OK' in line]),indent=2)+'\n')
    print(''.join(logs))
