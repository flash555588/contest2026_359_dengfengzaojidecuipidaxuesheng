#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/esp_start.c")
print("--- openvela esp_start includes ---")
for i, line in enumerate(p.read_text().splitlines()[:80], 1):
    print(f"{i}:{line}")
