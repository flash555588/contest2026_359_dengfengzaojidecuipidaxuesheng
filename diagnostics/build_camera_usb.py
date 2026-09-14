"""Build the isolated camera branch with the established soft-float runtime."""
from pathlib import Path
import os
import subprocess
import sys
import shutil
ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/camera-usb-fix'
root = Path('/tmp/v3-desktop-camera-usb-20260914')
env = os.environ.copy()
env['V3_BUILD_ROOT'] = str(root)
env['V3_BUILD_OVERLAY'] = str(delivery / 'overlay')
env['V3_BUILD_LOG'] = str(delivery / 'evidence/build.log')
subprocess.run([sys.executable, str(ws / 'diagnostics/build_linux.py'), *sys.argv[1:]], env=env, check=True)
if not sys.argv[1:]:
    for source, target in [('nuttx.bin', 'nuttx.bin'), ('nuttx', 'nuttx.elf'), ('nuttx.map', 'nuttx.map')]:
        shutil.copyfile(root / 'nuttx' / source, delivery / target)
