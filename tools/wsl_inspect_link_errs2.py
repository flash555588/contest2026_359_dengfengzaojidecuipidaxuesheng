#!/usr/bin/env python3
from pathlib import Path
import os

# C6 appinit
for p in Path("/home/flash/vela-p4/nuttx/boards/risc-v").rglob("*appinit.c"):
    print("====", p)
    print(p.read_text(encoding="utf-8", errors="replace"))
    print()

print("==== gpio reserve full ====")
print(Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_hw_support/esp_gpio_reserve.c").read_text())

print("==== libatomic search ====")
gccbin = Path("/home/flash/vela-p4/riscv32-esp-elf")
# find libatomic
n = 0
for dirpath, dirnames, filenames in os.walk(gccbin):
    for fn in filenames:
        if "atomic" in fn.lower():
            print(Path(dirpath)/fn)
            n += 1
print("count", n)

# How apache Make.defs links gcc libs
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/Make.defs")
t = p.read_text()
for line in t.splitlines():
    if any(x in line for x in ("LIBGCC", "EXTRA_LIBS", "atomic", "LDLIBS", "LIBS")):
        print("CMN", line)
