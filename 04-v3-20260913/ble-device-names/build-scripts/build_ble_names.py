"""Build device-name support in a private copy of the working BLE tree."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/ble-device-names'
old = Path('/tmp/v3-desktop-ble-startup-20260914')
root = Path('/tmp/v3-desktop-ble-names-20260914')
fresh = not root.exists()
if fresh:
    subprocess.run(['cp', '-a', str(old), str(root)], check=True)
    for path in root.rglob('Kconfig'):
        if path.is_file() and path.resolve().is_relative_to(root):
            before = path.read_text()
            after = before.replace(str(old) + '/', str(root) + '/')
            if before != after:
                path.write_text(after)
env = os.environ.copy()
env['V3_BUILD_ROOT'] = str(root)
env['V3_BUILD_OVERLAY'] = str(delivery / 'overlay')
steps = [(['clean'], 'clean.log'), ([], 'build.log')] if fresh else [([], 'build.log')]
for args, name in steps:
    env['V3_BUILD_LOG'] = str(delivery / 'evidence' / name)
    subprocess.run([sys.executable, str(workspace / 'diagnostics/build_linux.py'), *args], env=env, check=True)
for src, dst in [('nuttx.bin', 'nuttx.bin'), ('nuttx', 'nuttx.elf'), ('nuttx.map', 'nuttx.map')]:
    shutil.copyfile(root / 'nuttx' / src, delivery / dst)
assert (root / 'nuttx/.config').read_bytes() == (delivery / 'resolved.config').read_bytes()
print('Device names firmware SHA256:', hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest())
