#!/usr/bin/env python3
from pathlib import Path
import re

kth = Path("/home/flash/vela-p4/nuttx/include/nuttx/kthread.h").read_text(encoding="utf-8", errors="replace")
m = re.search(r"^int kthread_create\s*\([^;]*\);", kth, re.M | re.S)
print("==== kthread_create")
print(m.group(0) if m else "NOT FOUND")

# find nxtask_init implementation
hits = []
for p in Path("/home/flash/vela-p4/nuttx/sched").rglob("*.c"):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "nxtask_init(" in t:
        hits.append(str(p))
print("impl files", hits)

# task_tcb_s
sched = Path("/home/flash/vela-p4/nuttx/include/nuttx/sched.h").read_text(encoding="utf-8", errors="replace")
for name in ("struct task_tcb_s", "struct kthread_tcb_s", "struct tcb_s"):
    i = sched.find(name)
    print("====", name, "idx", i)
    if i >= 0:
        print(sched[i:i+500])
        print()

# rest of create_task in os.c
os_c = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/src/platform/os.c")
lines = os_c.read_text(encoding="utf-8", errors="replace").splitlines()
print("==== rest of create_task ====")
for n, line in enumerate(lines[1360:1420], 1361):
    print(f"{n:4d}|{line}")

# posix spawn flags
spawn = Path("/home/flash/vela-p4/nuttx/include/spawn.h").read_text(encoding="utf-8", errors="replace")
for s in ("POSIX_SPAWN_SETSCHEDPARAM", "POSIX_SPAWN_SETSTACKSIZE", "POSIX_SPAWN_SETSCHEDULER"):
    print(s, s in spawn)

# how nxtask_init uses attr
for p in Path("/home/flash/vela-p4/nuttx").rglob("*task_init*"):
    print("file", p)
