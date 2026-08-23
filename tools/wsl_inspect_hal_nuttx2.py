#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
for rel in [
    "components/bootloader_support/src/bootloader_init.c",
    "nuttx/esp32p4/include/sdkconfig.h",
]:
    p = hal / rel
    print("====", rel, p.exists())
    if not p.exists():
        continue
    t = p.read_text(encoding="utf-8", errors="replace")
    if "sdkconfig" in rel:
        for line in t.splitlines():
            if any(x in line for x in ("WDT", "NUTTX", "BOOTLOADER", "SIMPLE")):
                print(line)
        continue
    # print around __NuttX__
    lines = t.splitlines()
    for i, line in enumerate(lines):
        if "__NuttX__" in line:
            start = max(0, i-8)
            end = min(len(lines), i+20)
            print(f"--- around {i+1} ---")
            for j in range(start, end):
                print(f"{j+1}|{lines[j]}")
