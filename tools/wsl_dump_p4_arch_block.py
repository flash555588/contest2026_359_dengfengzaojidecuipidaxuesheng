#!/usr/bin/env python3
from pathlib import Path
ak = Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig").read_text().splitlines()
for n in range(100, 250):
    print(f"{n}:{ak[n-1]}")
print("--- Toolchain.defs CROSSDEV ---")
td = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/Toolchain.defs")
for line in td.read_text().splitlines():
    if "CROSSDEV" in line or "riscv" in line.lower() or "PREFIX" in line:
        print(line)
