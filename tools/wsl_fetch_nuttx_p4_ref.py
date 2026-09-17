#!/usr/bin/env python3
"""Sparse-clone Apache NuttX ESP32-P4 chip + board as a porting reference."""
import os
import subprocess

DEST = "/home/flash/nuttx-esp32p4-ref"
if not os.path.isdir(os.path.join(DEST, ".git")):
    os.makedirs(DEST, exist_ok=True)
    subprocess.check_call(
        [
            "git",
            "clone",
            "--depth",
            "1",
            "--filter=blob:none",
            "--sparse",
            "https://github.com/apache/nuttx.git",
            DEST,
        ]
    )
os.chdir(DEST)
subprocess.check_call(
    [
        "git",
        "sparse-checkout",
        "set",
        "boards/risc-v/esp32p4",
        "arch/risc-v/src/esp32p4",
        "arch/risc-v/include/esp32p4",
        "arch/risc-v/src/common",
        "arch/risc-v/include/common",
    ]
)
print("REF_OK", DEST)
