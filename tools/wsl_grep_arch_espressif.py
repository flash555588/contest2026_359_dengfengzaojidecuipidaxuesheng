#!/usr/bin/env python3
from pathlib import Path
root = Path("/home/flash/vela-p4/nuttx")
for p in root.rglob("Kconfig*"):
    try:
        text = p.read_text(errors="ignore")
    except Exception:
        continue
    if "ARCH_CHIP_ESPRESSIF" in text:
        print("FILE", p)
        for i, line in enumerate(text.splitlines(), 1):
            if "ARCH_CHIP_ESPRESSIF" in line:
                print(f"  {i}:{line}")
