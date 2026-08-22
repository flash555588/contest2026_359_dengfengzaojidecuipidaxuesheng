#!/usr/bin/env python3
from pathlib import Path
k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Kconfig")
lines = k.read_text().splitlines()
for n in range(268, 290):
    print(f"{n+1}:{lines[n]}")
print("--- pins ---")
for n in range(1020, 1068):
    print(f"{n+1}:{lines[n]}")
