#!/usr/bin/env python3
from pathlib import Path

sched = Path("/home/flash/vela-p4/nuttx/include/nuttx/sched.h")
text = sched.read_text(encoding="utf-8", errors="replace")
for name in ("nxtask_init", "nxtask_create", "nxtask_activate", "kthread_create", "task_create"):
    i = text.find(name)
    print("====", name, "in sched.h idx", i)
    if i >= 0:
        print(text[max(0, i - 200):i + 400])
        print()

os_c = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/src/platform/os.c")
print("==== os.c includes ====")
lines = os_c.read_text(encoding="utf-8", errors="replace").splitlines()
for n, line in enumerate(lines[:80], 1):
    print(f"{n:4d}|{line}")

print("==== os.c around 520-660 ====")
for n, line in enumerate(lines[510:670], 511):
    print(f"{n:4d}|{line}")

print("==== os.c around 1260-1360 ====")
for n, line in enumerate(lines[1255:1365], 1256):
    print(f"{n:4d}|{line}")
