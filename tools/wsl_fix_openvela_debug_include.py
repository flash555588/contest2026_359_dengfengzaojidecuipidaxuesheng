#!/usr/bin/env python3
from pathlib import Path

roots = [
    Path("/mnt/c/Users/flash/Desktop/openvela-contest/"
         "contest2026_359_dengfengzaojidecuipidaxuesheng/chip/esp32p4"),
    Path("/home/flash/openvela/contest2026_359_dengfengzaojidecuipidaxuesheng/"
         "chip/esp32p4"),
    Path("/home/flash/openvela/nuttx/arch/risc-v/src/common/espressif"),
]
n = 0
seen = set()
for root in roots:
    if not root.exists():
        continue
    for p in root.rglob("*"):
        if p.suffix not in {".c", ".h", ".S"}:
            continue
        key = str(p.resolve())
        if key in seen:
            continue
        seen.add(key)
        t = p.read_text(encoding="utf-8", errors="replace")
        if "<nuttx/debug.h>" not in t:
            continue
        p.write_text(t.replace("<nuttx/debug.h>", "<debug.h>"), encoding="utf-8")
        n += 1
        print(p)
print("updated", n)
