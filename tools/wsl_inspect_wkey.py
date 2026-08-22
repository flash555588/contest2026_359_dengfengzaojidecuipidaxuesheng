#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
for p in hal.rglob("*"):
    if p.suffix not in {".h", ".c"}:
        continue
    try:
        t = p.read_text(encoding="utf-8", errors="replace")
    except Exception:
        continue
    if "LP_WDT_SWD_WKEY_VALUE" in t or "TIMG_WDT_WKEY_VALUE" in t or "WDT_WKEY_VALUE" in t:
        for ln in t.splitlines():
            if "WKEY_VALUE" in ln and "define" in ln.lower():
                print(p.name, ln)
