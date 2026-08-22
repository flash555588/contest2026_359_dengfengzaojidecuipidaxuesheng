#!/usr/bin/env python3
from pathlib import Path

p = Path("/home/flash/vela-p4/nuttx/sched/task/task_init.c")
print(p.read_text(encoding="utf-8", errors="replace")[:8000])
print("\n==== spawnattr_t ====")
spawn = Path("/home/flash/vela-p4/nuttx/include/spawn.h").read_text(encoding="utf-8", errors="replace")
i = spawn.find("posix_spawnattr_t")
print(spawn[i:i+1500] if i>=0 else "no")

print("\n==== posix_spawnattr_setstacksize impl search ====")
for f in Path("/home/flash/vela-p4/nuttx").rglob("*.c"):
    t = f.read_text(encoding="utf-8", errors="replace")
    if "posix_spawnattr_setstacksize" in t and "int posix_spawnattr_setstacksize" in t:
        print(f)
        # print function
        i = t.find("int posix_spawnattr_setstacksize")
        print(t[i:i+800])
