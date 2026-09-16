#!/usr/bin/env python3
from pathlib import Path
p = Path("/usr/local/bin")
for f in sorted(p.iterdir()):
    n = f.name.lower()
    if "config" in n or "kconfig" in n:
        print(f.name)
print("--- sethost snippet ---")
lines = Path("/home/flash/vela-p4/nuttx/tools/sethost.sh").read_text().splitlines()
for i in range(150, 180):
    print(f"{i+1}:{lines[i]}")
