#!/usr/bin/env python3
from pathlib import Path
k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Kconfig")
print(k.read_text()[:3500])
print("===== ARCH_CHIP umbrella =====")
ak = Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig").read_text()
for i, line in enumerate(ak.splitlines(), 1):
    if "ESPRESSIF" in line or "ESP32C3" in line or "ESP32C6" in line or "ESP32H2" in line:
        if i < 280 or "default \"" in line or "source " in line:
            print(f"{i}:{line}")
print("===== Make.defs HAL clone =====")
mk = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Make.defs")
text = mk.read_text()
for i, line in enumerate(text.splitlines(), 1):
    if "HAL" in line or "CHIP_SERIES" in line or "esp32p4" in line.lower() or "VERSION" in line or "URL" in line:
        print(f"{i}:{line}")
