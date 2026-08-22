#!/usr/bin/env python3
from pathlib import Path
ak = Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig").read_text()
idx = ak.find("config ARCH_CHIP_ESPRESSIF")
print("idx", idx)
if idx >= 0:
    print(ak[idx:idx+500])
else:
    print("not in arch Kconfig")
    # search tree
    for p in Path("/home/flash/vela-p4/nuttx").rglob("Kconfig"):
        t = p.read_text(errors="ignore")
        if "config ARCH_CHIP_ESPRESSIF" in t:
            print("found", p)
            print(t[t.find("config ARCH_CHIP_ESPRESSIF"):t.find("config ARCH_CHIP_ESPRESSIF")+400])
            break
