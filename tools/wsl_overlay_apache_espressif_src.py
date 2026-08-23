#!/usr/bin/env python3
"""Overlay Apache ESP32-P4 common/espressif sources onto openvela's C3/C6/H2 layer.

Keeps openvela Kconfig + Make.defs (already patched for CHIP_SERIES=esp32p4).
"""
from __future__ import annotations

import os
import shutil
from pathlib import Path

DEST = Path(
    os.environ.get("OPENVELA_ROOT", "/home/flash/vela-p4")
) / "nuttx/arch/risc-v/src/common/espressif"
SRC = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/chip/esp32p4/common-espressif"
)
KEEP = {"Make.defs", "CMakeLists.txt"}
NUTTX_INCLUDE = Path(os.environ.get("OPENVELA_ROOT", "/home/flash/vela-p4")) / "nuttx/include/nuttx"


def main() -> int:
    if not SRC.is_dir() or not DEST.is_dir():
        raise SystemExit(f"missing {SRC} or {DEST}")
    n = 0
    for item in SRC.iterdir():
        if item.name in KEEP:
            continue
        dest = DEST / item.name
        if item.is_dir():
            if dest.exists():
                shutil.rmtree(dest)
            shutil.copytree(item, dest)
        else:
            shutil.copy2(item, dest)
        n += 1
    stub = NUTTX_INCLUDE / "debug.h"
    if not stub.exists():
        stub.write_text('#pragma once\n#include <debug.h>\n', encoding="utf-8")
        print("wrote", stub)
    print("overlayed", n, "entries into", DEST)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
