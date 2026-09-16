#!/usr/bin/env python3
from pathlib import Path
k = Path("/home/flash/nuttx-esp32p4-ref/arch/risc-v/Kconfig").read_text().splitlines()
for n in list(range(180, 250)) + list(range(640, 690)) + list(range(880, 930)):
    if 1 <= n <= len(k):
        print(f"{n}:{k[n-1]}")
