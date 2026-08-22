#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/components/bootloader_support/src/esp32p4/bootloader_esp32p4.c")
t = p.read_text(encoding="utf-8")
i = t.find("esp_err_t bootloader_init(void)")
print(t[i:i+2800])
print("==== includes head ====")
print("\n".join(t.splitlines()[:60]))
