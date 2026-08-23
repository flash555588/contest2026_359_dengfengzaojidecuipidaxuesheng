#!/usr/bin/env python3
from pathlib import Path
root = Path("/home/flash/vela-p4")
print("src esp32p4", (root/"nuttx/arch/risc-v/src/esp32p4").exists(), (root/"nuttx/arch/risc-v/src/esp32p4").is_symlink())
print("inc esp32p4", (root/"nuttx/arch/risc-v/include/esp32p4").exists())
print("board common", (root/"nuttx/boards/risc-v/esp32p4/common").exists())
print("vendor board", (root/"vendor/espressif/boards/esp32p4/esp32p4-function-ev-board").exists())
k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Kconfig")
print("--- common Kconfig first 30 ---")
print("\n".join(k.read_text().splitlines()[:30]))
ak = Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig")
text = ak.read_text()
print("ARCH_CHIP_ESP32P4" in text, 'default "esp32p4"' in text, "src/esp32p4/Kconfig" in text)
# show P4 snippets
for i, line in enumerate(text.splitlines(), 1):
    if "ESP32P4" in line or "esp32p4" in line:
        print(f"{i}:{line}")
