#!/usr/bin/env python3
"""Validate USB/BLE JSON adapters using real files and deterministic device IO."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=a.output.resolve();(out/'nuttx').mkdir(parents=True,exist_ok=True);(out/'nuttx/config.h').write_text('#pragma once\n')
d=root/'app/espdl-quickapp/overlay/apps/system/desktop';cj=a.apps/'netutils/cjson/cJSON'
flags=['-std=gnu11','-D_GNU_SOURCE','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-pthread']
# The production mount-path buffer models /mnt/usbN (32 bytes). Keep the
# filesystem fixture short and isolated, regardless of the artifact directory.
with tempfile.TemporaryDirectory(prefix='qif-') as fixture:
    flags+=['-DQPK_USB_MOUNT_ROOT="'+str(Path(fixture)/'mnt')+'"',*['-I'+str(i) for i in [out,d,cj,root/'app/espdl-quickapp/overlay/nuttx/include']]]
    subprocess.run(['gcc',*flags,str(root/'app/espdl-quickapp/tests/interface_protocols_host.c'),str(d/'glass_portal_files.c'),str(cj/'cJSON.c'),'-Wl,--gc-sections','-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
