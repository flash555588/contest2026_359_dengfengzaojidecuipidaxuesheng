#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
mwdt = (hal / "components/esp_hal_wdt/esp32p4/include/hal/mwdt_ll.h").read_text(encoding="utf-8", errors="replace")
print(mwdt[:3500])
print("==== TIMERG ====")
st = (hal / "components/soc/esp32p4/register/hw_ver1/soc/timer_group_struct.h").read_text(encoding="utf-8", errors="replace")
for line in st.splitlines():
    if "TIMERG" in line or "timg_dev" in line.lower() and "extern" in line:
        print(line)
print("==== DR_REG_TIMG0 ====")
# find in soc.h or periph
for p in [
    hal / "components/soc/esp32p4/register/hw_ver1/soc/soc.h",
    hal / "components/soc/esp32p4/include/soc/hw_ver1/soc.h",
    hal / "components/soc/esp32p4/register/hw_ver1/soc/reg_base.h",
]:
    if p.exists():
        t = p.read_text(encoding="utf-8", errors="replace")
        for line in t.splitlines():
            if "TIMG" in line:
                print(p.name, line)

# glob TIMG0
for p in (hal / "components/soc/esp32p4").rglob("*.h"):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "DR_REG_TIMG0_BASE" in t:
        for line in t.splitlines():
            if "DR_REG_TIMG0_BASE" in line:
                print("found", p, line)
        break
