#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/tools/configure.sh")
print("exists", p.exists())
text = p.read_text(errors="replace")
print(text[:2500])
print("--- usage later ---")
# print comments about custom
for i, line in enumerate(text.splitlines(), 1):
    if "custom" in line.lower() or "BOARD" in line and "config" in line.lower():
        print(f"{i}:{line}")
