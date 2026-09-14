"""Build the UI changes in an isolated copy of the verified storage tree."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/ui-layout-fix'
base = workspace / '04-v3-20260913/app-storage-fix'
old = Path('/tmp/v3-desktop-app-storage-20260914')
root = Path('/tmp/v3-desktop-ui-layout-20260914')
assert (delivery / 'resolved.config').read_bytes() == (base / 'resolved.config').read_bytes()
fresh = not root.exists()
if fresh:
    subprocess.run(['cp', '-a', str(old), str(root)], check=True)
    for path in root.rglob('Kconfig'):
        if path.is_file() and path.resolve().is_relative_to(root):
            before = path.read_text()
            after = before.replace(str(old) + '/', str(root) + '/')
            if after != before:
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
assert (root / 'nuttx/.config').read_bytes() == (base / 'resolved.config').read_bytes()
print('UI firmware SHA256:', hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest())
