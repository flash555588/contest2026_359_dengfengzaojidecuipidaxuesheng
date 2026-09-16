#!/usr/bin/env python3
"""Expand Apache NuttX sparse checkout for ESP32-P4 HAL/tools pieces."""
import os
import subprocess

DEST = "/home/flash/nuttx-esp32p4-ref"
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
        "arch/risc-v/Kconfig",
        "arch/risc-v/src/Makefile",
        "arch/risc-v/src/CMakeLists.txt",
        "tools/espressif",
        "tools/esp32p4",
        ".gitmodules",
        "CMakeLists.txt",
    ]
)
print("EXPAND_OK")
