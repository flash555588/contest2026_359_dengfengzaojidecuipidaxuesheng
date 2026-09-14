"""Verify the actual camera firmware, cumulative sources and data boundary."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys
ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/camera-usb-fix'
base = ws / '04-v3-20260913/ble-device-names'
allowed = {'nuttx/.config', 'apps/system/desktop/Makefile', 'apps/system/desktop/qpk_runtime.c', 'apps/system/desktop/camera_resource.c',
           'apps/system/desktop/desktop_main.c', 'nuttx/arch/risc-v/src/esp32p4/Make.defs',
           'apps/system/desktop/glass_ui_probe.inc',
           'nuttx/arch/risc-v/src/esp32p4/Kconfig',
           'nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_bringup.c'}
for p in (base / 'overlay').rglob('*'):
    if not p.is_file() or '__pycache__' in p.parts: continue
    relative = p.relative_to(base / 'overlay')
    if relative.as_posix() not in allowed:
        assert p.read_bytes() == (delivery / 'overlay' / relative).read_bytes(), str(relative)
config = (delivery / 'resolved.config').read_text().splitlines()
old = (base / 'resolved.config').read_text().splitlines()
assert [x for x in config if x and 'CONFIG_ESP32P4_CAMERA_USB' not in x] == [x for x in old if x]
image = (delivery / 'nuttx.bin').read_bytes()
assert image[0] == 0xe9 and image[23] == 1 and struct.unpack_from('<H', image, 12)[0] == 18
offset = 24
for _ in range(image[1]):
    _, size = struct.unpack_from('<II', image, offset)
    offset += 8 + size
offset = (offset + 16) & ~15
assert image[offset:offset + 32] == hashlib.sha256(image[:offset]).digest()
offset += 32
while offset < len(image):
    _, size = struct.unpack_from('<II', image, offset)
    assert offset + 8 + size <= len(image)
    offset += 8 + size
assert offset == len(image)
erase_end = 0x2000 + ((len(image) + 4095) & ~4095)
assert erase_end <= 0x400000
nm = 'D:/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin/riscv32-esp-elf-nm.exe'
symbols = subprocess.check_output([nm, '--defined-only', str(delivery / 'nuttx.elf')], text=True)
required = ['camera_usb_initialize', 'camera_usb_phy_ready', 'usbh_initialize', 'USBH_IRQHandler',
            'camera_uvc_parse', 'camera_uvc_payload', 'qpk_mjpeg_decode', 'qpk_camera_probe',
            'qpk_storage_selftest', 'glass_ble_scan', 'bluetooth_conn_alloc', 'c6net_initialize', 'dhcpc_request']
assert all(any(l.endswith(' ' + name) for l in symbols.splitlines()) for name in required)
assert 'PASS:' in (delivery / 'evidence/protocol-test.log').read_text()
test = subprocess.check_output(['node', str(ws / 'diagnostics/camera-tests/test_camera_app.js')], text=True)
(delivery / 'evidence/app-test.log').write_text(test)
assert 'PASS:' in test
source = (delivery / 'overlay/apps/system/desktop/camera/app.js').read_bytes()
assert source in image, 'Actual camera JS not present in image'
report = {'status':'static-checks-passed', 'firmware_sha256':hashlib.sha256(image).hexdigest(),
          'bytes':len(image), 'flash_offset':'0x2000', 'erase_end_exclusive':hex(erase_end),
          'data_offset':'0x400000', 'simpleboot_digest_valid':True,
          'cumulative_ble_wifi_storage_fixes_preserved':True, 'required_symbols':required,
          'hardware_stream_validation':'pending'}
(delivery / 'evidence/validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
