#!/usr/bin/env python3
"""Keep the ESP32-P4 ONESHOT_COUNT selection dependency-safe.

ONESHOT_COUNT is defined inside ``if ONESHOT``. Selecting it unconditionally
causes kconfiglib's refresh step to fail when ONESHOT is disabled.
"""
from pathlib import Path

KCONFIG = Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig")

k = KCONFIG.read_text(encoding="utf-8")
old = "\tselect ONESHOT_COUNT\n"
new = "\tselect ONESHOT_COUNT if ONESHOT\n"
start = k.find("config ARCH_CHIP_ESP32P4")
if start < 0:
    raise SystemExit("ARCH_CHIP_ESP32P4 missing")
end = k.find("\nconfig ARCH_CHIP_", start + 1)
block = k[start:end]

if new in block:
    print("Kconfig already has dependency-safe ONESHOT_COUNT selection")
elif old in block:
    k = k[:start] + block.replace(old, new, 1) + k[end:]
    KCONFIG.write_text(k, encoding="utf-8")
    print("Kconfig: select ONESHOT_COUNT if ONESHOT")
else:
    raise SystemExit("could not patch ARCH_CHIP_ESP32P4 ONESHOT_COUNT")
