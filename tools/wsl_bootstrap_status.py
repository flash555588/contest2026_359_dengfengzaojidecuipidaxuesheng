#!/usr/bin/env python3
from pathlib import Path
import os
log = Path("/home/flash/vela-p4/bootstrap.log")
print("pid 9035", os.path.exists("/proc/9035"))
print("log exists", log.exists(), "size", log.stat().st_size if log.exists() else 0)
if log.exists():
    print(log.read_text(errors="replace")[-3000:])
print("bootstrap_ok", (Path("/home/flash/vela-p4")/"bootstrap_ok.txt").exists())
print("nuttx git", (Path("/home/flash/vela-p4")/"nuttx"/".git").exists())
print("apps git", (Path("/home/flash/vela-p4")/"apps"/".git").exists())
