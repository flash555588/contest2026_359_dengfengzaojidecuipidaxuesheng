#!/usr/bin/env python3
"""Copy Apache NuttX ESP32-P4 board/common/chip into the contest repo."""
import os
import shutil
import subprocess

REF = "/home/flash/nuttx-esp32p4-ref"
CONTEST = "/mnt/c/Users/flash/Desktop/openvela-contest/contest2026_359_dengfengzaojidecuipidaxuesheng"

pairs = [
    (
        os.path.join(REF, "boards/risc-v/esp32p4/esp32p4-function-ev-board"),
        os.path.join(CONTEST, "board/esp32p4-function-ev-board"),
    ),
    (
        os.path.join(REF, "boards/risc-v/esp32p4/common"),
        os.path.join(CONTEST, "board/esp32p4-common"),
    ),
    (
        os.path.join(REF, "arch/risc-v/src/esp32p4"),
        os.path.join(CONTEST, "chip/esp32p4/src"),
    ),
    (
        os.path.join(REF, "arch/risc-v/include/esp32p4"),
        os.path.join(CONTEST, "chip/esp32p4/include"),
    ),
]

tools_src = os.path.join(REF, "tools/espressif")
if os.path.isdir(tools_src):
    pairs.append((tools_src, os.path.join(CONTEST, "chip/esp32p4/tools-espressif")))

for src, dst in pairs:
    if os.path.exists(dst):
        shutil.rmtree(dst)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    subprocess.check_call(["cp", "-a", src, dst])
    print("COPIED", src, "->", dst)

print("COPY_OK")
