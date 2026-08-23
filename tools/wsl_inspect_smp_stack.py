#!/usr/bin/env python3
from pathlib import Path
for p in [
    Path("/home/flash/vela-p4/nuttx/arch/risc-v/include/irq.h"),
    Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/riscv_internal.h"),
    Path("/home/flash/vela-p4/nuttx/include/nuttx/config.h"),
]:
    if not p.exists():
        print("missing", p)
        continue
    t = p.read_text(encoding="utf-8", errors="replace")
    for i, line in enumerate(t.splitlines(), 1):
        if "SMP_STACK" in line or "CONFIG_SMP " in line or "CONFIG_SMP=" in line or "IDLETHREAD_STACK" in line:
            if p.name == "config.h" and "SMP" not in line and "IDLE" not in line:
                continue
            print(p.name, i, line)
