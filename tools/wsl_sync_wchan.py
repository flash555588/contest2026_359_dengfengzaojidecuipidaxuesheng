#!/usr/bin/env python3
import os
from pathlib import Path

def wchan(pid):
    p = Path(f"/proc/{pid}/wchan")
    return p.read_text() if p.exists() else "?"

for pid in (485, 516, 531, 7941, 7942, 7943, 7944, 8794, 6274):
    st = Path(f"/proc/{pid}/status")
    if not st.exists():
        print(pid, "GONE")
        continue
    state = [ln for ln in st.read_text().splitlines() if ln.startswith("State:")][0]
    print(pid, state, "wchan=", wchan(pid))

print("--- nuttx.git refs ---")
os.system("git --git-dir=/home/flash/openvela/.repo/projects/nuttx.git log -1 --oneline 2>/dev/null || git --git-dir=/home/flash/openvela/.repo/project-objects/nuttx.git log -1 --oneline 2>/dev/null")
os.system("git --git-dir=/home/flash/openvela/.repo/projects/nuttx.git rev-parse --is-bare-repository 2>/dev/null; git --git-dir=/home/flash/openvela/.repo/projects/nuttx.git branch -a 2>/dev/null | head")

print("--- du ---")
os.system("du -sh /home/flash/openvela /home/flash/openvela/.repo 2>/dev/null")

print("--- overlay wait pid 6274 ---")
print("exists", Path("/proc/6274").exists())
