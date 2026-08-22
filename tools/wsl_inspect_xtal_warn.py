#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
for p in hal.rglob("*.c"):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "invalid RTC_XTAL_FREQ_REG" in t:
        print("FILE", p)
        i = t.find("invalid RTC_XTAL_FREQ_REG")
        print(t[max(0,i-800):i+1200])
        print("====")
