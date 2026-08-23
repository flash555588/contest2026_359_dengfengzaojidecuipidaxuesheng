#!/usr/bin/env python3
from pathlib import Path
root = Path("/home/flash/vela-p4")
for p in [
    root/"vendor/espressif/boards/esp32p4/esp32p4-function-ev-board",
    root/"nuttx/vendor/espressif/boards/esp32p4/esp32p4-function-ev-board",
    root/"nuttx/boards/risc-v/esp32p4",
]:
    print(p, "exists", p.exists(), "link", p.is_symlink(), "resolve", p.resolve() if p.exists() else None)

# find bringup
for p in root.rglob("esp32p4_bringup.c"):
    print("bringup", p, "link", p.is_symlink(), p.resolve())
