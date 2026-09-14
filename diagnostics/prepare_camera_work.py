"""Create an isolated camera build and export the HAL/decoder references."""
from pathlib import Path
import shutil
import subprocess

workspace = Path(__file__).resolve().parent.parent
old = Path('/tmp/v3-desktop-ble-names-20260914')
root = Path('/tmp/v3-desktop-camera-usb-20260914')
if not root.exists():
    subprocess.run(['cp', '-a', str(old), str(root)], check=True)
    for p in root.rglob('Kconfig'):
        if p.is_file() and p.resolve().is_relative_to(root):
            text = p.read_text()
            p.write_text(text.replace(str(old) + '/', str(root) + '/'))
out = workspace / 'diagnostics/camera-baseline'
paths = ['nuttx/arch/risc-v/src/esp32p4/hal_esp32p4.mk',
         'apps/system/desktop/Makefile',
         'nuttx/arch/risc-v/src/common/espressif/esp_irq.h',
         'nuttx/arch/risc-v/src/common/espressif/esp_cache.c',
         'nuttx/include/nuttx/kmalloc.h']
for directory in ['nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components',
                  'apps/graphics/lvgl/lvgl/src/libs']:
    for p in (root / directory).rglob('*'):
        if p.is_file() and (p.name in ['usb_utmi_hal.c', 'usb_utmi_hal.h', 'usb_utmi_ll.h'] or
                           'tjpgd' in p.name):
            paths.append(str(p.relative_to(root)))
for relative in paths:
    p = root / relative
    if p.is_file():
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, target)
        print(relative)
delivery = workspace / '04-v3-20260913/camera-usb-fix'
(delivery / 'evidence').mkdir(parents=True, exist_ok=True)
if not (delivery / 'overlay').exists():
    shutil.copytree(workspace / '04-v3-20260913/ble-device-names/overlay', delivery / 'overlay')
shutil.copyfile(old / 'nuttx/.config', delivery / 'resolved.config')
print('Camera workspace ready:', root)
