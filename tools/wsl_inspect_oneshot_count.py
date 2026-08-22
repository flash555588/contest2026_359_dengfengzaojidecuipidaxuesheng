#!/usr/bin/env python3
from pathlib import Path
import re

# Kconfig
for p in Path("/home/flash/vela-p4/nuttx").rglob("Kconfig"):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "ONESHOT_COUNT" in t:
        i = t.find("config ONESHOT_COUNT")
        print("====", p)
        print(t[i:i+800] if i>=0 else t[t.find("ONESHOT_COUNT")-200:t.find("ONESHOT_COUNT")+400])
        print()

cfg = Path("/home/flash/vela-p4/nuttx/.config").read_text(encoding="utf-8", errors="replace")
for line in cfg.splitlines():
    if "ONESHOT" in line or "ALARM" in line or "ARCH_HAVE_TICKLESS" in line or "SCHED_TICKLESS" in line or "SYSTEM_TIME64" in line:
        print("CFG", line)

# oneshot_count_init
for p in Path("/home/flash/vela-p4/nuttx/drivers").rglob("*oneshot*"):
    print("driver", p)

oneshot_c = list(Path("/home/flash/vela-p4/nuttx").rglob("*oneshot*.c"))
print("oneshot c files", oneshot_c)
for p in oneshot_c:
    t = p.read_text(encoding="utf-8", errors="replace")
    if "oneshot_count_init" in t:
        print("has oneshot_count_init", p)
