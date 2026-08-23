#!/usr/bin/env python3
from pathlib import Path
root = Path("/home/flash/nuttx-esp32p4-ref")
k = (root / "arch/risc-v/Kconfig").read_text().splitlines()
for i, line in enumerate(k):
    if "ESP32P4" in line or "esp32p4" in line:
        print(f"{i+1}:{line}")
print("--- Makefile ---")
mk = (root / "arch/risc-v/src/Makefile").read_text().splitlines()
for i, line in enumerate(mk):
    if "ESP32" in line or "esp32" in line:
        print(f"{i+1}:{line}")
print("--- CMakeLists ---")
cm = (root / "arch/risc-v/src/CMakeLists.txt").read_text().splitlines()
for i, line in enumerate(cm):
    if "ESP32" in line or "esp32" in line or "CHIP" in line:
        print(f"{i+1}:{line}")
