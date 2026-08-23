#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/components/bootloader_support/src/esp32p4/bootloader_esp32p4.c")
print(p.read_text(encoding="utf-8", errors="replace"))
