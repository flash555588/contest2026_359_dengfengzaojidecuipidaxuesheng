#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
# find bootloader_clear_bss_section
for p in hal.rglob("*.c"):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "bootloader_clear_bss_section" in t and "void bootloader_clear_bss" in t:
        i = t.find("bootloader_clear_bss_section")
        print("FILE", p)
        print(t[i:i+800])
        print("---")

print("==== wdt_hal.h context ====")
h = (hal / "components/esp_hal_wdt/include/hal/wdt_hal.h").read_text(encoding="utf-8", errors="replace")
print(h[:2500])

print("==== TIMERG PROVIDE in ld ====")
ldroot = Path("/home/flash/vela-p4/nuttx")
for p in list(ldroot.rglob("*.ld"))[:80]:
    t = p.read_text(encoding="utf-8", errors="replace")
    if "TIMERG0" in t:
        print(p)
        for line in t.splitlines():
            if "TIMERG" in line:
                print(line)
