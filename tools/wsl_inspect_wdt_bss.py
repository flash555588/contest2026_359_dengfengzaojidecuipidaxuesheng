#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
h = (hal / "components/esp_hal_wdt/include/hal/wdt_hal.h").read_text(encoding="utf-8", errors="replace")
print("==== wdt_hal snippets ====")
for key in ("RWDT_HAL_CONTEXT", "MWDT", "set_flashboot", "wdt_hal_disable", "wdt_hal_context"):
    print("--", key)
    for i, ln in enumerate(h.splitlines(), 1):
        if key in ln:
            print(f"{i}|{ln}")

mp = Path("/home/flash/vela-p4/nuttx/nuttx.map")
text = mp.read_text(encoding="utf-8", errors="replace")
print("==== bss/stack symbols ====")
for name in ("_sbss", "_ebss", "_bss_start", "_bss_end", "_sheap", "_eheap",
             "g_idle_topstack", "__stack", "_dram"):
    n=0
    for line in text.splitlines():
        if name in line and "0x" in line and n<3:
            print(line[:200]); n+=1

print("==== linker IRAM/DRAM ====")
# from nuttx.ld or map memory config
for line in text.splitlines()[:80]:
    if "Memory Configuration" in line or "iram" in line.lower() or "dram" in line.lower() or "sram" in line.lower():
        print(line[:200])
