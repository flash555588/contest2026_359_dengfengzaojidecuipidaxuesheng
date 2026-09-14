"""Build voice input without changing the camera delivery or original WSL tree."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ws = Path(__file__).resolve().parent.parent
root = Path('/tmp/v3-desktop-voice-20260915')
delivery = ws / '04-v3-20260913/espclaw-voice'
env = os.environ.copy()
env.update(V3_BUILD_ROOT=str(root), V3_BUILD_OVERLAY=str(delivery / 'overlay'),
           V3_BUILD_LOG=str(delivery / 'evidence/build.log'))
subprocess.run([sys.executable, str(ws / 'diagnostics/build_linux.py'), *sys.argv[1:]],
               env=env, check=True)
if not sys.argv[1:]:
    for a, b in [('nuttx.bin', 'nuttx.bin'), ('nuttx', 'nuttx.elf'), ('nuttx.map', 'nuttx.map')]:
        shutil.copyfile(root / 'nuttx' / a, delivery / b)
