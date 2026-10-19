#!/usr/bin/env python3
"""Verify the production ASR wake observer with real authenticated model PCM."""
import argparse,subprocess
from pathlib import Path
from wake_streaming import stage_sources
p=argparse.ArgumentParser(description=__doc__)
for name in ['runtime','fixtures','output']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
sources=stage_sources(desktop,a.output/'source')
binary=a.output/'voice-wake-observer'
subprocess.run(['gcc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(desktop),
 str(repo/'app/espdl-quickapp/tests/voice_wake_observer_host.c'),*map(str,sources),'-lm','-o',str(binary)],check=True)
subprocess.run([str(binary),str(a.runtime),str(a.fixtures/'wake-positive.pcm'),str(a.fixtures/'wake-negative.pcm')],check=True,timeout=30)
