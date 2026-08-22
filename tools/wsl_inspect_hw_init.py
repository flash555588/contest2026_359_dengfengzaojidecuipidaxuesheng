#!/usr/bin/env python3
from pathlib import Path
mp = Path("/home/flash/vela-p4/nuttx/nuttx.map")
text = mp.read_text(encoding="utf-8", errors="replace")
for name in ("wdt_hal_set_flashboot_en", "wdt_hal_write_protect_disable",
             "wdt_hal_disable", "TIMERG0", "_bss_start", "_bss_end",
             "bootloader_init"):
    print("====", name)
    n=0
    for line in text.splitlines():
        if name in line:
            print(line[:220])
            n+=1
            if n>=8:
                break
    if n==0:
        print("NOT FOUND")

print("==== bootloader_esp32p4 hardware_init ====")
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/components/bootloader_support/src/esp32p4/bootloader_esp32p4.c")
t = p.read_text(encoding="utf-8", errors="replace")
i = t.find("bootloader_hardware_init")
print(t[i:i+2200])
print("==== ana_reset ====")
i = t.find("bootloader_ana_reset_config")
print(t[i:i+900])
print("==== super_wdt ====")
i = t.find("bootloader_super_wdt_auto_feed")
print(t[i:i+700])
