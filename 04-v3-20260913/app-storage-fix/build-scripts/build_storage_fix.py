"""Build/test a separate app storage fix without changing the verified config."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/app-storage-fix'
base = workspace / '04-v3-20260913/wifi-dhcp-fix'
old_root = Path('/tmp/v3-desktop-wifi-dhcp-20260914')
root = Path('/tmp/v3-desktop-app-storage-20260914')
source = delivery / 'overlay/apps/system/desktop'
assert (base / 'resolved.config').read_bytes() == (delivery / 'resolved.config').read_bytes()
with tempfile.TemporaryDirectory(prefix='qpk-host-') as host:
    executable = str(Path(host) / 'test')
    with (delivery / 'evidence/host-tests.log').open('w') as log:
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(source), str(source / 'qpk_storage.c'),
                        str(workspace / 'diagnostics/test_qpk_storage.c'),
                        '-Wl,--wrap=fopen,--wrap=mkdir,--wrap=rename,--wrap=unlink',
                        '-o', executable], check=True, stdout=log, stderr=subprocess.STDOUT)
        subprocess.run([executable], check=True, stdout=log, stderr=subprocess.STDOUT)
    print((delivery / 'evidence/host-tests.log').read_text(), flush=True)
if '--test-only' in sys.argv:
    raise SystemExit(0)
if not root.exists():
    print('Cloning the verified isolated Wi-Fi build.', flush=True)
    subprocess.run(['cp', '-a', str(old_root), str(root)], check=True)
for path in root.rglob('Kconfig'):
    if path.is_file() and path.resolve().is_relative_to(root):
        before = path.read_text()
        after = before
        for previous in ['/home/flash/glass-ble-v3-20260913',
                         '/tmp/v3-desktop-black-screen-20260914', str(old_root)]:
            after = after.replace(previous + '/', str(root) + '/')
        if after != before:
            path.write_text(after)
env = os.environ.copy()
env['V3_BUILD_ROOT'] = str(root)
env['V3_BUILD_OVERLAY'] = str(delivery / 'overlay')
steps = [([], 'build.log')] if '--incremental' in sys.argv else [(['clean'], 'clean.log'), ([], 'build.log')]
for arguments, name in steps:
    env['V3_BUILD_LOG'] = str(delivery / 'evidence' / name)
    subprocess.run([sys.executable, str(workspace / 'diagnostics/build_linux.py'), *arguments],
                   env=env, check=True)
for original, target in [('nuttx.bin', 'nuttx.bin'), ('nuttx', 'nuttx.elf'), ('nuttx.map', 'nuttx.map')]:
    shutil.copyfile(root / 'nuttx' / original, delivery / target)
assert (root / 'nuttx/.config').read_bytes() == (delivery / 'resolved.config').read_bytes()
print('Built storage fix:', hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest(), flush=True)
