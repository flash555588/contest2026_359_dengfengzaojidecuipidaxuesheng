#!/usr/bin/env python3
"""Check the production phrase loudness normalizer with sanitizers."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
executable=out/'gain-host'
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',
  '-I'+str(repo/'app/espdl-quickapp/overlay/apps/system/desktop'),str(repo/'app/espdl-quickapp/tests/speech_gain_host.c'),'-o',str(executable)],check=True)
subprocess.run([str(executable)],check=True,timeout=30)
