#!/usr/bin/env python3
import os
import shutil
import subprocess
from pathlib import Path

REF = Path("/home/flash/nuttx-esp32p4-ref")
CONTEST = Path("/mnt/c/Users/flash/Desktop/openvela-contest/contest2026_359_dengfengzaojidecuipidaxuesheng")

src = REF / "arch/risc-v/src/common/espressif"
dst = CONTEST / "chip/esp32p4/common-espressif"
print("src exists", src.is_dir(), "count", len(list(src.rglob("*"))) if src.is_dir() else 0)
if dst.exists():
    shutil.rmtree(dst)
if src.is_dir():
    subprocess.check_call(["cp", "-a", str(src), str(dst)])
    print("COPIED common-espressif")

inc = REF / "arch/risc-v/include/common"
print("include/common", inc.is_dir(), list(inc.iterdir())[:20] if inc.is_dir() else None)

k = (REF / "arch/risc-v/Kconfig").read_text()
start = k.find("config ARCH_CHIP_ESP32P4")
print("=== ARCH_CHIP_ESP32P4 block ===")
print(k[start:start+1800] if start >= 0 else "NOT FOUND")

mk = (REF / "arch/risc-v/src/Makefile").read_text()
for line in mk.splitlines():
    if "esp32p4" in line.lower() or "ESP32P4" in line:
        print("MK:", line)

cm = (REF / "arch/risc-v/src/CMakeLists.txt").read_text()
for line in cm.splitlines():
    if "esp32p4" in line.lower() or "ESP32P4" in line:
        print("CM:", line)
