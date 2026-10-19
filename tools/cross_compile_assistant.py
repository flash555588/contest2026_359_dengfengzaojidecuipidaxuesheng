#!/usr/bin/env python3
"""Compile assistant changes using the prepared P4 flags without changing it."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess

p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
env={**os.environ,'PATH':'/home/flash/vela-p4/toolchains/gcc14/bin:'+os.environ['PATH']}
command=['make','-s','-C',str(a.baseline/'apps/system/desktop'),'APPDIR='+str(a.baseline/'apps'),'TOPDIR='+str(a.baseline/'nuttx'),
         'CROSSDEV=/home/flash/vela-p4/toolchains/gcc14/bin/riscv32-esp-elf-',
         '--eval=assistant-flags:;$(info ASSISTANT_CC=$(CC))$(info ASSISTANT_CFLAGS=$(CFLAGS))','assistant-flags']
r=subprocess.run(command,capture_output=True,text=True,env=env);(a.output/'flags.log').write_text(r.stdout+r.stderr);r.check_returncode()
def flags(prefix):return shlex.split(next(line[len(prefix):] for line in r.stdout.splitlines() if line.startswith(prefix)))
cc=flags('ASSISTANT_CC=');options=flags('ASSISTANT_CFLAGS=')
sources=[desktop/name for name in ['glass_board_assistant.c','glass_assistant_platform.c','glass_agent.c','qpk_storage.c','qpk_espdl_service.c','qpk_runtime.c','glass_speech.c','desktop_main.c']]
sources.append(desktop.parent/'espclaw/port/espclaw_qpk.c')
commands=[]
with (a.output/'compile.log').open('w') as log:
    for source in sources:
        cmd=[*cc,'-I'+str(desktop),'-I'+str(repo/'app/bilibili_live'),'-I'+str(desktop.parent/'espclaw/include'),
             '-I'+str(a.baseline/'apps/system/desktop/espdl_port/include'),
             '-I'+str(a.baseline/'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/esp_common/include'),
             *options,'-I'+str(a.baseline/'apps/system/desktop'),'-DGLASS_LOCAL_SPEECH',
             '-c',str(source),'-o',str(a.output/(source.name+'.o'))]
        commands.append(cmd);run=subprocess.run(cmd,cwd=a.baseline/'apps/system/desktop',env=env,stdout=log,stderr=subprocess.STDOUT)
        if run.returncode:
            log.flush();(a.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');print(source.name+' failed');print((a.output/'compile.log').read_text(errors='replace'));raise SystemExit(run.returncode)
(a.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
(a.output/'verification.json').write_text(json.dumps(dict(target='ESP32-P4 RV32 ILP32F',object_compile_only=True,firmware_linked=False,flashed=False,
    source_sha256={str(s.relative_to(repo)):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}),indent=2)+'\n')
print('ASSISTANT_RV32_COMPILE_OK: '+', '.join(s.name for s in sources))
