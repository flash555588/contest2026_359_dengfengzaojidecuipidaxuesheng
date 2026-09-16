#!/usr/bin/env python3
from pathlib import Path
cfg = Path("/home/flash/vela-p4/nuttx/.config").read_text(encoding="utf-8", errors="replace")
for s in ("RTC", "XTAL", "CLK_SRC", "SPIRAM", "MSPI", "REGION_PROTECTION", "BROWNOUT"):
    print("====", s)
    n=0
    for ln in cfg.splitlines():
        if s in ln and not ln.startswith("# ") or (s in ln and "CONFIG" in ln):
            if s in ln:
                print(ln)
                n+=1
                if n>=15:
                    break
