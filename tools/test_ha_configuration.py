"""Exercise production HA store/service and TCP receiver under sanitizers."""
from pathlib import Path
import argparse, subprocess
p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop';hass=desktop.parent/'hass'
a.output.mkdir(parents=True,exist_ok=True);(a.output/'nuttx').mkdir(exist_ok=True)
(a.output/'nuttx/config.h').write_text('#define CONFIG_SYSTEM_HASS 1\n')
cjson=a.apps/'netutils/cjson/cJSON'
flags=['-std=gnu11','-O1','-g','-D_GNU_SOURCE','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I'+str(a.output),'-I'+str(desktop),'-I'+str(hass),'-I'+str(cjson)]
for name,sources in [
 ('portal',[repo/'app/espdl-quickapp/tests/ha_portal_configuration_host.c',desktop/'glass_portal_config.c',hass/'hass_service.c',cjson/'cJSON.c']),
 ('service',[repo/'app/espdl-quickapp/tests/hass_configuration_host.c',hass/'hass_service.c',cjson/'cJSON.c']),
 ('transport',[repo/'app/espdl-quickapp/tests/hass_transport_growth_host.c'])]:
    binary=a.output/name
    subprocess.run(['gcc',*flags,*map(str,sources),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
