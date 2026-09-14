from pathlib import Path
import re
p = Path('/tmp/v3-desktop-camera-usb-20260914/nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/esp_hal_usb')
for name in ['esp32p4/include/hal/usb_dwc_ll.h', 'usb_dwc_hal.c']:
    lines = (p / name).read_text().splitlines()
    selected = set()
    for i, s in enumerate(lines):
        if re.search(r'hcchar_disable|ahb_idle|core_soft_reset|soft_reset|hcfg.*|hctsiz.*', s, re.I):
            selected.update(range(max(0, i-2), min(len(lines), i+8)))
    print(name)
    print('\n'.join(f'{i+1}: {lines[i]}' for i in sorted(selected)))
