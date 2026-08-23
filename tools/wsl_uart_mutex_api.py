#!/usr/bin/env python3
from pathlib import Path
mh = Path("/home/flash/vela-p4/nuttx/include/nuttx/mutex.h")
print("exists", mh.exists())
for i, line in enumerate(mh.read_text().splitlines(), 1):
    if "nxmutex_" in line or "define nxmutex" in line:
        print(f"{i}:{line}")
print("--- openvela kconfig UART ---")
k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Kconfig")
for i, line in enumerate(k.read_text().splitlines(), 1):
    if "UART0" in line or "TXPIN" in line or "RXPIN" in line:
        print(f"{i}:{line}")
