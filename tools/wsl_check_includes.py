#!/usr/bin/env python3
from pathlib import Path
root = Path("/home/flash/vela-p4/nuttx/include")
for rel in ["debug.h", "nuttx/debug.h", "nuttx/config.h", "arch/chip/irq.h", "arch/board/board.h"]:
    p = root / rel
    print(("OK " if p.exists() else "NO "), rel, "->", p.resolve() if p.exists() else "")
print("include/nuttx count", len(list((root/"nuttx").glob("*.h"))) if (root/"nuttx").exists() else 0)
print("chip irq", Path("/home/flash/vela-p4/nuttx/arch/risc-v/include/esp32p4/irq.h").exists())
hal_irq = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/nuttx/esp32p4/include/irq.h")
print("hal irq", hal_irq.exists(), hal_irq)
