#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/components/hal/include/hal/wdt_hal.h")
if not p.exists():
    p = next(Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty").rglob("wdt_hal.h"))
print(p)
t = p.read_text(encoding="utf-8", errors="replace")
print(t[:4000])
