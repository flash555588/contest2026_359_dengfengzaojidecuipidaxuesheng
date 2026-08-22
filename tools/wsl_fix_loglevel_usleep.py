#!/usr/bin/env python3
from pathlib import Path
import os
root = Path("/home/flash/vela-p4/nuttx")
# nxsched_usleep
hits = []
for p in [root/"include", root/"sched"]:
    for f in p.rglob("*.h"):
        try:
            t = f.read_text(errors="ignore")
        except Exception:
            continue
        if "nxsched_usleep" in t or "nxsig_usleep" in t:
            hits.append(str(f))
print("headers", hits[:20])
# log level in apache overlay kconfig on windows is in common-espressif but we kept openvela kconfig
k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Kconfig")
for i, line in enumerate(k.read_text().splitlines(), 1):
    if "LOG_LEVEL" in line or "ESP_LOG" in line:
        print(f"{i}:{line}")
# sdkconfig.h line
sdk = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/esp32p4/include/sdkconfig.h")
lines = sdk.read_text().splitlines()
print("--- sdkconfig 660-680 ---")
for n in range(659, min(680, len(lines))):
    print(f"{n+1}:{lines[n]}")
# os.h 350
os_h = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/include/platform/os.h")
ol = os_h.read_text().splitlines()
print("--- os.h 340-365 ---")
for n in range(339, min(365, len(ol))):
    print(f"{n+1}:{ol[n]}")
