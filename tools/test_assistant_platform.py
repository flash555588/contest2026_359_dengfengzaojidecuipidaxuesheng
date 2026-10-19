#!/usr/bin/env python3
"""Check production adapters and the real capture/classification worker offline."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import tempfile
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop';a.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='assistant-platform-') as temp:
    root=Path(temp);(root/'nuttx/video').mkdir(parents=True);(root/'nuttx/config.h').write_text('#pragma once\n')
    shutil.copy2(a.baseline/'nuttx/include/nuttx/video/uvc_camera.h',root/'nuttx/video/uvc_camera.h')
    flags=['-D_GNU_SOURCE','-DGLASS_LOCAL_SPEECH','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined',
           '-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-I'+str(root),'-I'+str(desktop),
           '-I'+str(a.baseline/'apps/netutils/cjson/cJSON'),'-DASSISTANT_PHOTO_ROOT="'+str(root/'photos')+'"']
    command=['gcc',*flags,str(repo/'app/espdl-quickapp/tests/assistant_platform_host.c'),str(desktop/'qpk_espdl_service.c'),
             str(desktop/'glass_agent_config.c'),str(desktop/'glass_chat_store.c'),
             str(a.baseline/'apps/netutils/cjson/cJSON/cJSON.c'),'-Wl,--gc-sections','-lm','-o',str(root/'test')]
    logs=[];env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'}
    for command in [command,[str(root/'test')]]:
        run=subprocess.run(command,text=True,capture_output=True,env=env);logs.append(run.stdout+run.stderr)
        (a.output/'test.log').write_text(''.join(logs),encoding='utf-8')
        if run.returncode:print(logs[-1]);raise SystemExit(run.returncode)
    print(''.join(logs));(a.output/'verification.json').write_text(json.dumps(dict(passed=True,scope='Production adapters and real ESP-DL worker; camera hardware/model, weather provider and audio are fixtures.',markers=[s for s in ''.join(logs).splitlines() if '_OK:' in s]),indent=2)+'\n')
