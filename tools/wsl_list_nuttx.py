#!/usr/bin/env python3
from pathlib import Path
import os

root = Path("/home/flash/openvela")
nuttx = root / "nuttx"
print("nuttx exists", nuttx.exists(), "is_dir", nuttx.is_dir(), "is_symlink", nuttx.is_symlink())
if nuttx.exists():
    entries = list(nuttx.iterdir())
    print("count", len(entries))
    for p in sorted(entries)[:40]:
        print(("D " if p.is_dir() else "F "), p.name)

print("--- vendor/espressif ---")
ve = root / "vendor/espressif"
if ve.exists():
    for p in sorted(ve.iterdir())[:30]:
        print(("D " if p.is_dir() else "F "), p.name)

print("--- prebuilts ---")
pb = root / "prebuilts"
if pb.exists():
    for p in sorted(pb.iterdir()):
        print(("D " if p.is_dir() else "F "), p.name)

print("--- .repo/projects sample ---")
proj = root / ".repo/projects"
if proj.exists():
    # count git dirs
    gits = list(proj.rglob("*.git"))
    print("git project dirs", len(gits))
    names = sorted(x.name for x in proj.iterdir()) if proj.is_dir() else []
    print("top", names[:40])

print("--- contest wsl ---")
c = root / "contest2026_359_dengfengzaojidecuipidaxuesheng"
print("exists", c.exists())
if c.exists():
    print("board", list((c/"board").iterdir()) if (c/"board").exists() else None)
