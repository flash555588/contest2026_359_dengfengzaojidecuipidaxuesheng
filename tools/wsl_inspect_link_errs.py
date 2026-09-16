#!/usr/bin/env python3
from pathlib import Path

# C6/C3 appinit
root = Path("/home/flash/vela-p4/nuttx/boards")
for p in root.rglob("*appinit*"):
    if "esp32" in str(p).lower():
        print("====", p)
        print(p.read_text(encoding="utf-8", errors="replace")[:2500])
        print()

# atomic in nuttx / libgcc
print("==== libatomic ====")
gcc = Path("/home/flash/vela-p4/riscv32-esp-elf")
for p in gcc.rglob("libatomic*"):
    print(p)

# gpio reserve
g = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_hw_support/esp_gpio_reserve.c")
print("==== gpio reserve ====")
print(g.read_text(encoding="utf-8", errors="replace")[:2000])

# EXTRA_LIBS in chip Make.defs
for p in [
    Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4/Make.defs"),
    Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Make.defs"),
    Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32c6/Make.defs"),
]:
    if p.exists():
        t = p.read_text(encoding="utf-8", errors="replace")
        if "atomic" in t.lower() or "EXTRA_LIBS" in t or "LIBGCC" in t:
            print("====", p)
            for line in t.splitlines():
                if any(x in line for x in ("atomic", "EXTRA_LIBS", "LIBGCC", "ldflags", "LDFLAGS")):
                    print(line)
