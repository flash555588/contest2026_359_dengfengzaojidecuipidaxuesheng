#!/usr/bin/env python3
import os
from pathlib import Path
root = Path("/home/flash/openvela")
print("pid 485", os.path.exists("/proc/485"))
if os.path.exists("/proc/485"):
    cmd = Path("/proc/485/cmdline").read_bytes().replace(b"\x00", b" ").decode()
    print("cmd", cmd[:300])
    print("status", Path("/proc/485/status").read_text().split("State:")[1].split("\n")[0].strip())
projects = root / ".repo" / "projects"
print("projects dir", projects.exists())
if projects.exists():
    names = [p.name for p in projects.iterdir()]
    print("project count", len(names))
print("has nuttx", (root/"nuttx").exists())
print("has vendor/espressif", (root/"vendor/espressif").exists())
# children sizes
for p in sorted(root.iterdir()):
    if p.name.startswith("."):
        continue
    print(p.name)
