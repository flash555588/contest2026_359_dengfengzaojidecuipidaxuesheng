#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
cr = (hal / "components/soc/include/soc/chip_revision.h").read_text(encoding="utf-8", errors="replace")
print(cr[:4000])
print("==== bootloader_init_mem ====")
for p in hal.rglob("bootloader_mem.c"):
    t = p.read_text(encoding="utf-8", errors="replace")
    i = t.find("bootloader_init_mem")
    print(p)
    print(t[i:i+1500])
print("==== cpu set_ivt ====")
for p in list(hal.rglob("esp_cpu.h"))[:5]:
    t = p.read_text(encoding="utf-8", errors="replace")
    if "set_ivt_addr" in t:
        i = t.find("esp_cpu_intr_set_ivt_addr")
        print(p)
        print(t[i:i+900])
print("==== mtvt ====")
for p in list(hal.rglob("*.h")):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "esp_cpu_intr_set_mtvt_addr" in t:
        i = t.find("esp_cpu_intr_set_mtvt_addr")
        print(p)
        print(t[i:i+700])
        break
