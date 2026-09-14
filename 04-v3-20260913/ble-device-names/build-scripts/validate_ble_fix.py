"""Check the BLE image and preserve the verified UI/storage/boot baseline."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--delivery', type=Path, default=root / '04-v3-20260913/ble-startup-fix')
parser.add_argument('--base', type=Path, default=root / '04-v3-20260913/ui-layout-fix')
args = parser.parse_args()
delivery, base = args.delivery, args.base
assert (delivery / 'resolved.config').read_bytes() == (base / 'resolved.config').read_bytes()
changed = {'apps/system/c6ble/ble_socket_register.c', 'nuttx/net/bluetooth/bluetooth_conn.c',
           'apps/wireless/bluetooth/nimble/mynewt-nimble/porting/npl/nuttx/src/os_callout.c',
           'nuttx/net/procfs/net_procfs.c',
           'apps/system/c6ble/ble_hosted.c',
           'apps/system/c6probe/esp_hosted.c',
           'nuttx/net/bluetooth/bluetooth_finddev.c', 'nuttx/net/bluetooth/bluetooth_sendmsg.c',
           'nuttx/wireless/bluetooth/bt_netdev.c'}
if delivery.name == 'ble-device-names':
    changed |= {'apps/system/desktop/glass_ble.inc', 'apps/system/desktop/glass_ble.h',
                'apps/wireless/bluetooth/nimble/glass_ble.h',
                'apps/wireless/bluetooth/nimble/glass_ble.c',
                'apps/wireless/bluetooth/nimble/glass_ble_name.h'}
    assert (delivery / 'overlay/apps/system/desktop/glass_ble.h').read_bytes() == (
        delivery / 'overlay/apps/wireless/bluetooth/nimble/glass_ble.h').read_bytes()
for path in (base / 'overlay').rglob('*'):
    relative = path.relative_to(base / 'overlay')
    if path.is_file() and '__pycache__' not in path.parts and relative.as_posix() not in changed:
        assert path.read_bytes() == (delivery / 'overlay' / relative).read_bytes(), relative
image = (delivery / 'nuttx.bin').read_bytes()
assert image[0] == 0xe9 and image[23] == 1
assert struct.unpack_from('<H', image, 12)[0] == 18
offset = 24
for _ in range(image[1]):
    _, length = struct.unpack_from('<II', image, offset)
    offset += 8 + length
offset = (offset + 16) & ~15
assert image[offset:offset + 32] == hashlib.sha256(image[:offset]).digest()
offset += 32
while offset < len(image):
    _, length = struct.unpack_from('<II', image, offset)
    assert offset + 8 + length <= len(image)
    offset += 8 + length
assert offset == len(image)
erase_end = 0x2000 + ((len(image) + 4095) & ~4095)
assert erase_end <= 0x400000
nm = 'D:/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin/riscv32-esp-elf-nm.exe'
symbols = subprocess.check_output([nm, '--defined-only', str(delivery / 'nuttx.elf')], text=True)
required = ['glass_ble_scan', 'glass_ble_start', 'bluetooth_conn_alloc', 'bluetooth_find_device',
            'c6_ble_register', 'qpk_storage_selftest', 'c6net_initialize', 'dhcpc_request', 'heap_caps_free']
assert all(any(line.endswith(' ' + name) for line in symbols.splitlines()) for name in required)
assert 'emac' not in symbols.lower()
(delivery / 'evidence/image-info.txt').write_bytes(subprocess.check_output(
    [sys.executable, '-m', 'esptool', '--chip', 'esp32p4', 'image-info', str(delivery / 'nuttx.bin')]))
report = {'status': 'static-checks-passed', 'firmware_sha256': hashlib.sha256(image).hexdigest(),
          'bytes': len(image), 'flash_offset': '0x2000', 'erase_end_exclusive': hex(erase_end),
          'data_offset': '0x400000', 'config_identical_to_ui_fix': True,
          'ui_storage_boot_fixes_preserved': True, 'simpleboot_digest_valid': True,
          'baseline_package': base.name,
          'required_symbols': required}
(delivery / 'evidence/validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
