#!/usr/bin/env python3
"""Focused real HA service/AI tool checks; scripted transport, no household writes."""
from pathlib import Path
import argparse,subprocess,os
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];d=root/'app/espdl-quickapp/overlay/apps/system/desktop';h=d.parent/'hass';t=root/'app/espdl-quickapp/tests';c=a.apps/'netutils/cjson/cJSON'
a.output.mkdir(parents=True,exist_ok=True);(a.output/'nuttx').mkdir(exist_ok=True);(a.output/'nuttx/config.h').write_text('#pragma once\n')
flags=['-std=gnu11','-O1','-g','-D_GNU_SOURCE','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-pthread','-I'+str(a.output),'-I'+str(d),'-I'+str(c),'-I'+str(h)]
sources=[t/'board_home_host.c',d/'glass_agent.c',d/'glass_board_home.c',d/'glass_agent_config.c',d/'glass_portal_config.c',d/'glass_chat_store.c',h/'hass_service.c',t/'board_agent_platform_host.c',d/'qpk_hardware.c',c/'cJSON.c']
binary=a.output/'home-functions-host'
with (a.output/'compile.log').open('w') as log:subprocess.run(['gcc',*flags,*map(str,sources),'-Wl,--gc-sections','-lm','-o',str(binary)],stdout=log,stderr=subprocess.STDOUT,check=True)
with (a.output/'host.log').open('w') as log:subprocess.run([str(binary)],stdout=log,stderr=subprocess.STDOUT,check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
print((a.output/'host.log').read_text())
