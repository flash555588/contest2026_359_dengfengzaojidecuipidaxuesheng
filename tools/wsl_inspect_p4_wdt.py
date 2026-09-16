#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/components/bootloader_support/src/esp32p4/bootloader_esp32p4.c")
print("exists", p.exists())
if p.exists():
    t = p.read_text(encoding="utf-8", errors="replace")
    for i, line in enumerate(t.splitlines(), 1):
        if any(x in line.lower() for x in ("wdt", "watchdog", "nuttx", "spi_flash", "bootloader_init")):
            print(f"{i:4d}|{line}")
