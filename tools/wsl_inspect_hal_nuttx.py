#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
n = 0
for p in hal.rglob("bootloader*.c"):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "__NuttX__" in t or "CONFIG_NUTTX" in t:
        print(p.relative_to(hal))
        n += 1
print("nuttx-guarded bootloader files", n)

# compile flags for this file
mk = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Make.defs")
t = mk.read_text(encoding="utf-8", errors="replace")
for line in t.splitlines():
    if "NuttX" in line or "CFLAGS" in line and "ESP" in line:
        print("MK", line)

# sdkconfig WDT
sdk = list(hal.rglob("sdkconfig.h"))
print("sdkconfig count", len(sdk))
for p in sdk[:5]:
    tt = p.read_text(encoding="utf-8", errors="replace")
    if "WDT" in tt or "BOOTLOADER" in tt:
        print("====", p)
        for line in tt.splitlines():
            if "WDT" in line or "BOOTLOADER_WDT" in line or "NUTTX" in line:
                print(line)
