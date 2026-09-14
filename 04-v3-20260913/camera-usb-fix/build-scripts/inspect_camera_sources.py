"""Export camera/USB source references from the currently validated private tree."""
from pathlib import Path
import shutil

root = Path('/tmp/v3-desktop-ble-names-20260914')
workspace = Path(__file__).resolve().parent.parent
out = workspace / 'diagnostics/camera-baseline'
for directory in ['nuttx/arch/risc-v/src/esp32p4', 'nuttx/drivers/usbhost',
                  'nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/src']:
    folder = root / directory
    print(directory)
    for path in folder.iterdir():
        if path.is_file() and ('usb' in path.name or 'camera' in path.name or path.name in ['Kconfig', 'CMakeLists.txt']):
            print(' ', path.name)
for relative in ['apps/system/desktop/qpk_runtime.c', 'apps/system/desktop/qpk_runtime.h',
                 'apps/system/desktop/camera_resource.c',
                 'nuttx/arch/risc-v/src/esp32p4/Kconfig',
                 'nuttx/arch/risc-v/src/esp32p4/Make.defs',
                 'nuttx/drivers/usbhost/Kconfig',
                 'nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_bringup.c',
                 'nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_camera.c']:
    source = root / relative
    if source.exists():
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
for directory in ['nuttx/drivers/usbhost', 'nuttx/drivers/video',
                  'nuttx/arch/xtensa/src/esp32s3', 'nuttx/arch/arm/src/common']:
    for source in (root / directory).rglob('*'):
        if source.is_file() and source.suffix in ['.c', '.h'] and any(key in source.name for key in ['uvc', 'usbhost', 'dwc2']):
            target = out / source.relative_to(root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
print('Export complete')
