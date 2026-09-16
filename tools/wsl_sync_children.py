#!/usr/bin/env python3
import os
from pathlib import Path

def children(pid):
    out = []
    for p in Path("/proc").iterdir():
        if not p.name.isdigit():
            continue
        try:
            stat = (p / "stat").read_text().split()
            ppid = int(stat[3])
            if ppid == pid:
                cmd = (p / "cmdline").read_bytes().replace(b"\x00", b" ").decode()[:200]
                out.append((int(p.name), cmd))
        except Exception:
            pass
    return out

print("children of 485:")
kids = children(485)
for pid, cmd in kids:
    print(pid, cmd)
    for cpid, ccmd in children(pid):
        print(" ", cpid, ccmd)
print("--- git ---")
os.system("ps -u flash -o pid,stat,etime,cmd | grep -E 'git|lfs|repo' | grep -v grep | head -40")
