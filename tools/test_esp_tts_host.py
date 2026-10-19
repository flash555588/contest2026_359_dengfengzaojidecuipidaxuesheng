#!/usr/bin/env python3
"""Check the production ESP-TTS adapter using an inert vendor API fixture."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prepared',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
a.output.mkdir(parents=True,exist_ok=True)
executable=a.output/'esp-tts-host'
subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O1','-g',
    '-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
    '-I'+str(desktop),'-I'+str(a.prepared/'upstream/esp-tts/esp_tts_chinese/include'),
    str(desktop/'glass_esp_tts.c'),str(desktop/'glass_esp_tts_voice.c'),str(repo/'app/espdl-quickapp/tests/esp_tts_host.c'),
    '-o',str(executable)],check=True)
subprocess.run([str(executable)],check=True,timeout=30)
