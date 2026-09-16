#!/usr/bin/env python3
from pathlib import Path
root = Path("/home/flash/vela-p4/nuttx")
for p in list((root/"arch/risc-v").rglob("*.h")) + list((root/"include").rglob("*.h")):
    try:
        t = p.read_text(encoding="utf-8", errors="replace")
    except Exception:
        continue
    if "SMP_STACK_SIZE" in t:
        print("====", p)
        for i, line in enumerate(t.splitlines(), 1):
            if "SMP_STACK" in line:
                print(f"{i}|{line}")
