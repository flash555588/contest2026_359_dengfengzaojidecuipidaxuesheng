#!/usr/bin/env python3
from pathlib import Path
import re

def proto(path, names):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    for name in names:
        m = re.search(rf"^[^/\n#]*\b{name}\s*\([^;]*\);", text, re.M | re.S)
        print("====", name, path)
        print(m.group(0) if m else "NOT FOUND")
        print()

proto("/home/flash/vela-p4/nuttx/include/nuttx/sched.h",
      ["nxtask_init", "nxtask_create", "nxtask_activate", "nxsched_get_tcb"])
proto("/home/flash/vela-p4/nuttx/include/nuttx/kthread.h", ["kthread_create"])
proto("/home/flash/vela-p4/nuttx/include/spawn.h",
      ["posix_spawnattr_init", "posix_spawnattr_setstacksize",
       "posix_spawnattr_setschedparam", "posix_spawnattr_setflags"])

# How ESP32 C3/C6 HAL in openvela creates tasks
import os
root = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src")
for p in root.rglob("*.c"):
    try:
        t = p.read_text(encoding="utf-8", errors="replace")
    except Exception:
        continue
    if "kthread_create" in t or "nxtask_create(" in t:
        if "esp" in str(p).lower() or "espressif" in str(p).lower() or "hal" in str(p).lower():
            print("REF", p)
            for i, line in enumerate(t.splitlines(), 1):
                if "kthread_create" in line or "nxtask_create(" in line or "nxtask_init(" in line:
                    print(f"  {i}: {line}")

print("==== file_mq_open ====")
for hdr in [
    "/home/flash/vela-p4/nuttx/include/nuttx/mqueue.h",
    "/home/flash/vela-p4/nuttx/include/nuttx/fs/fs.h",
]:
    p = Path(hdr)
    if not p.exists():
        continue
    t = p.read_text(encoding="utf-8", errors="replace")
    if "file_mq_open" in t or "file_fcntl" in t:
        print(hdr, "has symbols")
        for name in ("file_mq_open", "file_fcntl"):
            m = re.search(rf"^[^/\n#]*\b{name}\s*\([^;]*\);", t, re.M | re.S)
            if m:
                print(m.group(0)[:400])

print("==== fcntl.h exists", Path("/home/flash/vela-p4/nuttx/include/fcntl.h").exists())
print("==== TCB_FLAG", )
sched = Path("/home/flash/vela-p4/nuttx/include/nuttx/sched.h").read_text(encoding="utf-8", errors="replace")
for s in ("TCB_FLAG_TTYPE_KERNEL", "TCB_FLAG_FREE_TCB", "TCB_FLAG_FREE_STACK"):
    print(s, s in sched)
