#!/usr/bin/env python3
import os
from pathlib import Path

root = Path("/home/flash/openvela")
gcc = root / "prebuilts/gcc"
print("=== prebuilts/gcc ===")
if gcc.exists():
    for p in sorted(gcc.iterdir()):
        print(p.name, "->", [x.name for x in p.iterdir()] if p.is_dir() else "")

nuttx_git = root / ".repo/projects/nuttx.git"
print("=== nuttx.git ===", nuttx_git.exists())
if nuttx_git.exists():
    print("entries", [p.name for p in nuttx_git.iterdir()][:20])

# IO of current git fetch
for pid in (8794, 8795, 8796, 8803, 485):
    io = Path(f"/proc/{pid}/io")
    if io.exists():
        print(f"--- pid {pid} io ---")
        print(io.read_text())

# network connections
os.system("ss -tp 2>/dev/null | grep -E 'git|curl|python' | head -20")

print("=== project working trees with CMakeLists or Kconfig ===")
count_ok = 0
for p in sorted(root.iterdir()):
    if p.name.startswith("."):
        continue
    if p.is_dir():
        count_ok += 1
print("top-level dirs", count_ok)
