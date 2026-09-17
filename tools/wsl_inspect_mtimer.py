#!/usr/bin/env python3
from pathlib import Path

oneshot = Path("/home/flash/vela-p4/nuttx/include/nuttx/timers/oneshot.h")
text = oneshot.read_text(encoding="utf-8", errors="replace")
i = text.find("struct oneshot_operations_s")
print("==== oneshot_operations_s ====")
print(text[i:i+2500] if i>=0 else "NOT FOUND")

mt = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/riscv_mtimer.c")
print("\n==== riscv_mtimer.c head ====")
print("\n".join(mt.read_text(encoding="utf-8", errors="replace").splitlines()[:180]))

print("\n==== git origin of mtimer ====")
