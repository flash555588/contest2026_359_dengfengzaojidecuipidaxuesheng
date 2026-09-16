#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
tg = (hal / "components/soc/esp32p4/register/hw_ver1/soc/timer_group_reg.h").read_text(encoding="utf-8", errors="replace")
print("==== TIMG WDT regs ====")
for ln in tg.splitlines():
    if any(s in ln for s in ("WDTWPROTECT", "WDTCONFIG0", "WDT_WKEY", "WDTCONFIG0_REG", "TIMG_WDTWPROTECT_REG", "TIMG_WDTCONFIG0_REG")):
        print(ln)

lp = (hal / "components/soc/esp32p4/register/hw_ver1/soc/lp_wdt_reg.h").read_text(encoding="utf-8", errors="replace")
print("==== LP WDT (first 180 lines of useful) ====")
for ln in lp.splitlines():
    if any(s in ln for s in ("WPROTECT", "WKEY", "CONFIG0_REG", "SWD_", "FLASHBOOT", "WDT_EN", "define LP_WDT")):
        if ln.startswith("#define") or ln.startswith("/**") or "REG" in ln:
            print(ln[:200])

print("==== wdt_hal_init addr ====")
mp = Path("/home/flash/vela-p4/nuttx/nuttx.map").read_text(encoding="utf-8", errors="replace")
for line in mp.splitlines():
    if "wdt_hal_init" in line and "0x" in line and ".group" not in line:
        print(line[:220])
