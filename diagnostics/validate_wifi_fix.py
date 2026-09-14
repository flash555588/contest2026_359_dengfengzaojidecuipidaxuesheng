"""Validate the Wi-Fi image, configuration delta, and linked network drivers."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import struct
import subprocess
import sys
sys.dont_write_bytecode = True

workspace = Path(__file__).resolve().parent.parent
base = workspace / '04-v3-20260913/black-screen-fix'
delivery = workspace / '04-v3-20260913/wifi-dhcp-fix'
evidence = delivery / 'evidence'

def config_values(path):
    return dict(line.split('=', 1) for line in path.read_text().splitlines()
                if line.startswith('CONFIG_'))

before = config_values(base / 'resolved.config')
after = config_values(delivery / 'resolved.config')
changed = {key: {'before': before.get(key), 'after': after.get(key)}
           for key in sorted(before.keys() | after.keys()) if before.get(key) != after.get(key)}
assert set(changed) >= {'CONFIG_ESPRESSIF_EMAC', 'CONFIG_NSH_NETINIT'}
assert all(key in {'CONFIG_ESPRESSIF_EMAC', 'CONFIG_NSH_NETINIT', 'CONFIG_ARCH_PHY_INTERRUPT'}
           or key.startswith('CONFIG_ESPRESSIF_ETH_') for key in changed), changed
assert all(item['after'] is None for item in changed.values())
assert after['CONFIG_ESPRESSIF_HR_TIMER'] == 'y'
for name in ['CONFIG_SYSTEM_DESKTOP', 'CONFIG_SYSTEM_C6PROBE', 'CONFIG_SYSTEM_C6_DESKTOP',
             'CONFIG_NETUTILS_DHCPC', 'CONFIG_ESPRESSIF_MIPI_DSI', 'CONFIG_ESPRESSIF_SPIRAM']:
    assert after[name] == 'y', name

for p in (base / 'overlay').rglob('*'):
    if p.is_file() and '__pycache__' not in p.parts:
        assert p.read_bytes() == (delivery / 'overlay' / p.relative_to(base / 'overlay')).read_bytes()

spec = importlib.util.spec_from_file_location('digest', delivery / 'overlay/nuttx/tools/espressif/simple_boot_digest.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
image = (delivery / 'nuttx.bin').read_bytes()
assert image == module.add_digest(image)
assert image[0] == 0xe9 and image[23] == 1
assert struct.unpack_from('<H', image, 12)[0] == 18
offset = 24
for _ in range(image[1]):
    _, length = struct.unpack_from('<II', image, offset)
    offset += 8 + length
offset = (offset + 16) & ~15
assert image[offset:offset+32] == hashlib.sha256(image[:offset]).digest()
offset += 32
segments = []
while offset < len(image):
    address, length = struct.unpack_from('<II', image, offset)
    assert offset + 8 + length <= len(image)
    if address:
        segments.append({'file_offset': hex(offset), 'address': hex(address), 'bytes': length})
    offset += 8 + length
assert offset == len(image) and len(segments) >= 2
erase_end = 0x2000 + ((len(image) + 4095) & ~4095)
assert erase_end <= 0x400000

nm = Path('D:/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin/riscv32-esp-elf-nm.exe')
result = subprocess.run([str(nm), '--defined-only', str(delivery / 'nuttx.elf')],
                        capture_output=True, text=True, check=True)
symbols = {line.split()[-1] for line in result.stdout.splitlines() if line.split()}
needed = ['c6net_initialize', 'c6net_daemon', 'c6net_rx', 'dhcpc_request',
          'esp_timer_impl_early_init', 'heap_caps_free', 'qpk_runtime_launch']
for name in needed:
    assert name in symbols, name
assert not any('emac' in name.lower() for name in symbols)
# HA retains an optional netinit fallback if launching NSH fails. Verify
# the normal NSH/desktop startup paths, rather than requiring dead symbols.
objdump = nm.with_name('riscv32-esp-elf-objdump.exe')
startup = ''
for name in ['nsh_initialize', 'desktop_main']:
    assert name in symbols, name
    disassembly = subprocess.run([str(objdump), '-d', '--disassemble=' + name,
                                  str(delivery / 'nuttx.elf')],
                                 capture_output=True, text=True, check=True)
    assert '<' + name + '>:' in disassembly.stdout
    assert '<netinit_bringup>' not in disassembly.stdout
    startup += disassembly.stdout
(evidence / 'startup-disassembly.txt').write_text(startup)
(evidence / 'network-symbols.txt').write_text('\n'.join(line for line in result.stdout.splitlines()
    if any(key in line for key in ['c6net', 'dhcpc', 'esp_timer', 'heap_caps_free', 'qpk_runtime_launch']))+'\n')

report = {'status': 'build-and-static-checks-passed-hardware-pending',
          'firmware_sha256': hashlib.sha256(image).hexdigest(), 'bytes': len(image),
          'flash_offset': '0x2000', 'flash_end_exclusive': hex(0x2000 + len(image)),
          'erase_end_exclusive': hex(erase_end), 'data_offset': '0x400000',
          'ram_sha256_valid': True, 'mapped_segments': segments,
          'emac_symbols_absent': True, 'normal_startup_does_not_call_netinit': True,
          'required_symbols_present': needed, 'config_changes': changed,
          'black_screen_overlay_preserved': True,
          'flashed': False, 'dhcp_on_target': 'not-tested'}
(evidence / 'validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
