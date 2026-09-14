"""Build a separate Wi-Fi profile using the verified desktop and toolchain."""
from pathlib import Path
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/wifi-dhcp-fix'
base = workspace / '04-v3-20260913/black-screen-fix'
old_root = Path('/tmp/v3-desktop-black-screen-20260914')
root = Path('/tmp/v3-desktop-wifi-dhcp-20260914')
delivery.mkdir(exist_ok=True)
(delivery / 'evidence').mkdir(exist_ok=True)
if not root.exists():
    print('Copying the isolated build; original nuttxspace is read-only.', flush=True)
    subprocess.run(['cp', '-a', str(old_root), str(root)], check=True)
# Generated application Kconfigs in the export retain the author's absolute
# build path. Relocate only this disposable copy before invoking Kconfig.
for path in root.rglob('Kconfig'):
    if path.is_file() and path.resolve().is_relative_to(root):
        contents = path.read_text()
        updated = contents.replace('/home/flash/glass-ble-v3-20260913/', str(root) + '/')
        updated = updated.replace(str(old_root) + '/', str(root) + '/')
        if updated != contents:
            path.write_text(updated)
for source in (base / 'overlay').rglob('*'):
    if source.is_file() and '__pycache__' not in source.parts:
        dest = delivery / 'overlay' / source.relative_to(base / 'overlay')
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)

before = (base / 'resolved.config').read_text()
config = before
for symbol in ['ESPRESSIF_EMAC', 'NSH_NETINIT']:
    assert f'CONFIG_{symbol}=y\n' in config
    config = config.replace(f'CONFIG_{symbol}=y\n', f'# CONFIG_{symbol} is not set\n')
(root / 'nuttx/.config').write_text(config)
env = os.environ.copy()
env['PATH'] = '/home/streetartist/.local/bin:' + env['PATH']
with (delivery / 'evidence/reconfigure.log').open('w') as log:
    result = subprocess.run(['make', 'olddefconfig'], cwd=root / 'nuttx', env=env,
                            stdout=log, stderr=subprocess.STDOUT)
if result.returncode:
    print((delivery / 'evidence/reconfigure.log').read_text())
    raise SystemExit(result.returncode)
reference = (root / 'nuttx/.config').read_text()
(delivery / 'evidence/kconfig-reference.config').write_text(reference)
# The exported profile has intentional hidden settings (including HR_TIMER)
# which olddefconfig resets. Apply only the networking dependency closure to
# that previously verified profile; preserve all unrelated build settings.
def symbol(line):
    match = re.match(r'(?:# )?(CONFIG_[A-Z0-9_]+)(?:=| is not set)', line)
    return match.group(1) if match else None

def network_setting(key):
    return key in {'CONFIG_ESPRESSIF_EMAC', 'CONFIG_NSH_NETINIT',
                   'CONFIG_ARCH_PHY_INTERRUPT'} or bool(key and key.startswith('CONFIG_ESPRESSIF_ETH_'))

reference_lines = {symbol(line): line for line in reference.splitlines(True) if symbol(line)}
config = ''.join(reference_lines.get(symbol(line), '') if network_setting(symbol(line)) else line
                 for line in before.splitlines(True))
(root / 'nuttx/.config').write_text(config)
assert 'CONFIG_ESPRESSIF_EMAC=y' not in config
assert 'CONFIG_NSH_NETINIT=y' not in config
for symbol in ['SYSTEM_C6PROBE', 'SYSTEM_DESKTOP', 'NETUTILS_DHCPC',
               'INTERPRETERS_QUICKJS', 'SYSTEM_ESPCLAW', 'SYSTEM_C6_DESKTOP']:
    assert f'CONFIG_{symbol}=y' in config, symbol
shutil.copyfile(root / 'nuttx/.config', delivery / 'resolved.config')
config_overlay = delivery / 'overlay/nuttx/.config'
config_overlay.parent.mkdir(parents=True, exist_ok=True)
config_overlay.write_text(config)
delta = ''.join(difflib.unified_diff(before.splitlines(True), config.splitlines(True),
                                    'a/nuttx/.config', 'b/nuttx/.config'))
(delivery / 'network-config.patch').write_text(delta)
(delivery / 'full-fix.patch').write_text((base / 'black-screen.patch').read_text()+delta)
print('Network configuration delta:', delta, flush=True)
env['V3_BUILD_ROOT'] = str(root)
env['V3_BUILD_OVERLAY'] = str(delivery / 'overlay')
env['V3_BUILD_LOG'] = str(delivery / 'evidence/clean.log')
subprocess.run([sys.executable, str(workspace / 'diagnostics/build_linux.py'), 'clean'],
               env=env, check=True)
env['V3_BUILD_LOG'] = str(delivery / 'evidence/build.log')
subprocess.run([sys.executable, str(workspace / 'diagnostics/build_linux.py')],
               env=env, check=True)
for source, target in [('nuttx.bin', 'nuttx.bin'), ('nuttx', 'nuttx.elf'), ('nuttx.map', 'nuttx.map')]:
    shutil.copyfile(root / 'nuttx' / source, delivery / target)
assert (root / 'nuttx/.config').read_bytes() == (delivery / 'resolved.config').read_bytes()
print('Built Wi-Fi fix:', hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest(), flush=True)
