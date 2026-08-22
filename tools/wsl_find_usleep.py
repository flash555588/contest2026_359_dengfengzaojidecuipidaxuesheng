#!/usr/bin/env python3
from pathlib import Path
sig = Path("/home/flash/vela-p4/nuttx/include/nuttx/signal.h").read_text()
for i, line in enumerate(sig.splitlines(), 1):
    if "usleep" in line.lower() or "nxsched" in line:
        print(f"{i}:{line}")

# also sched.h
for name in ["include/nuttx/sched.h", "include/sched.h", "sched/sched.h"]:
    p = Path("/home/flash/vela-p4/nuttx")/name
    if p.exists():
        t = p.read_text(errors="ignore")
        if "usleep" in t:
            print("FILE", p)
            for i, line in enumerate(t.splitlines(), 1):
                if "usleep" in line:
                    print(f"  {i}:{line}")
