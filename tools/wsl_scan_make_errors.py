#!/usr/bin/env python3
from pathlib import Path
t = Path("/home/flash/vela-p4/nsh_build_outer.log").read_text(errors="replace")
i = t.rfind("+ make -j2")
chunk = t[i:]
for line in chunk.splitlines():
    low = line.lower()
    if "error:" in low or "fatal" in low or "undefined" in low or "Error" in line[:20] or "missing" in low:
        print(line[:400])
print("--- last 30 lines ---")
print("\n".join(chunk.splitlines()[-30:])[:2500])
