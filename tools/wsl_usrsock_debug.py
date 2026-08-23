#!/usr/bin/env python3
from pathlib import Path
cfg = Path("/home/flash/vela-p4/nuttx/.config").read_text().splitlines()
keys = ("USRSOCK", "CONFIG_NET=", "CONFIG_WIRELESS", "CONFIG_DRIVERS", "CONFIG_ESP32P4_WIFI", "CONFIG_ESPRESSIF_WIFI", "CONFIG_NETDEVICES")
for line in cfg:
    if any(k in line for k in keys):
        if line.startswith("CONFIG_") or line.startswith("# CONFIG_"):
            print(line)

# show the usrsock.h area and who includes it
print("--- drivers_initialize includes ---")
di = Path("/home/flash/vela-p4/nuttx/drivers/drivers_initialize.c")
if di.exists():
    for i, line in enumerate(di.read_text().splitlines()[:80], 1):
        if "include" in line or "USRSOCK" in line or "net" in line:
            print(f"{i}:{line}")

u = Path("/home/flash/vela-p4/nuttx/include/nuttx/net/usrsock.h")
print("usrsock exists", u.exists())
if u.exists():
    lines = u.read_text().splitlines()
    for n in range(310, min(340, len(lines))):
        print(f"{n+1}:{lines[n]}")
