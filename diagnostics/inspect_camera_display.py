"""Copy the actual isolated build's display sources for review."""
from pathlib import Path
import shutil
ws = Path(__file__).resolve().parent.parent
root = Path('/tmp/v3-desktop-camera-usb-20260914')
out = ws / 'diagnostics/camera-display-reference'
files = list((root / 'nuttx/boards/risc-v/esp32p4').rglob('*lcd*.c'))
files += list((root / 'apps/graphics/lvgl/lvgl/src/drivers/nuttx').rglob('*fbdev*.c'))
files += [root / 'nuttx/drivers/video/fb.c']
files += list((root / 'nuttx/arch/risc-v/src/common/espressif').glob('*mipi_dsi*'))
files += list((root / 'nuttx/arch/risc-v/src/esp32p4').glob('*cache*.c'))
files += list((root / 'nuttx/arch/risc-v/src/common/espressif').glob('*cache*.c'))
files += list((root / 'nuttx/arch/risc-v/src/common').glob('*cache*.c'))
files += list((root / 'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/esp_mm').glob('*cache*.c'))
files += [root / 'nuttx/include/nuttx/cache.h', root / 'nuttx/include/nuttx/arch.h']
for source in files:
    target = out / source.relative_to(root)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    print(source.relative_to(root), source.stat().st_size)
