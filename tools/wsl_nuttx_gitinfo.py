#!/usr/bin/env python3
from pathlib import Path
g = Path("/home/flash/openvela/.repo/projects/nuttx.git")
print("symlink", g.is_symlink())
if g.is_symlink():
    print("target", g.readlink())
print("HEAD", (g/"HEAD").read_text() if (g/"HEAD").exists() else None)
print("config")
print((g/"config").read_text() if (g/"config").exists() else None)
print("packed-refs head")
pr = g/"packed-refs"
if pr.exists():
    lines = pr.read_text().splitlines()[:15]
    print("\n".join(lines))
print("objects size")
import os
os.system("du -sh /home/flash/openvela/.repo/projects/nuttx.git /home/flash/openvela/.repo/project-objects 2>/dev/null | head")
