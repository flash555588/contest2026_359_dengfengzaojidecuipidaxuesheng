#!/usr/bin/env python3
from pathlib import Path

# ARCH_CHIP_ESP32P4 kconfig
for p in [
    Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig"),
    Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4/Kconfig"),
]:
    if not p.exists():
        print("missing", p)
        continue
    t = p.read_text(encoding="utf-8", errors="replace")
    i = t.find("config ARCH_CHIP_ESP32P4")
    print("====", p, "idx", i)
    if i >= 0:
        print(t[i:i+900])
        print()

# oneshot_count_init definition
oneshot = Path("/home/flash/vela-p4/nuttx/include/nuttx/timers/oneshot.h").read_text(encoding="utf-8", errors="replace")
i = oneshot.find("oneshot_count_init")
print("==== oneshot_count_init in header idx", i)
print(oneshot[max(0,i-200):i+700] if i>=0 else "not in header")

drv = Path("/home/flash/vela-p4/nuttx/drivers/timers/oneshot.c").read_text(encoding="utf-8", errors="replace")
print("oneshot.c has count_init", "oneshot_count_init" in drv)
print("oneshot.c ONESHOT_COUNT", "ONESHOT_COUNT" in drv)

# how other chips select it
k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig").read_text(encoding="utf-8", errors="replace")
i = k.find("config ARCH_CHIP_ESP32C6")
print("==== ESP32C6 ====")
print(k[i:i+700] if i>=0 else "no")
i = k.find("config ARCH_CHIP_ESP32H2")
print("==== ESP32H2 ====")
print(k[i:i+700] if i>=0 else "no")
