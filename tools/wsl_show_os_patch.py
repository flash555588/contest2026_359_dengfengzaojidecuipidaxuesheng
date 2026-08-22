#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/src/platform/os.c")
lines = p.read_text(encoding="utf-8").splitlines()
print("==== includes ====")
for n, line in enumerate(lines[:45], 1):
    print(f"{n:4d}|{line}")
print("==== create_task locals+init ====")
for n, line in enumerate(lines[1300:1365], 1301):
    print(f"{n:4d}|{line}")
