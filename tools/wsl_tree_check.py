#!/usr/bin/env python3
from pathlib import Path

root = Path("/home/flash/openvela")
checks = [
    "nuttx/arch/risc-v/Kconfig",
    "nuttx/tools/build.sh",
    "build.sh",
    "nuttx/arch/risc-v/src/esp32c3",
    "nuttx/arch/risc-v/src/esp32p4",
    "nuttx/arch/risc-v/src/common/espressif",
    "nuttx/tools/espressif",
    "vendor/espressif/boards/esp32s3/esp32s3-eye",
    "prebuilts/gcc/linux-x86_64/riscv-none-elf",
    "overlay_done.txt",
    "overlay_wait.log",
]
for rel in checks:
    p = root / rel
    print(("OK " if p.exists() else "NO "), rel)

k = root / "nuttx/arch/risc-v/Kconfig"
if k.exists():
    text = k.read_text(errors="replace")
    print("Kconfig ESP32P4", "ARCH_CHIP_ESP32P4" in text)
    print("Kconfig ESP32C3", "ARCH_CHIP_ESP32C3" in text)
    print("Kconfig ESP32H2", "ARCH_CHIP_ESP32H2" in text)

wait = root / "overlay_wait.log"
if wait.exists():
    print("--- overlay_wait.log ---")
    print(wait.read_text(errors="replace")[-1500:])
