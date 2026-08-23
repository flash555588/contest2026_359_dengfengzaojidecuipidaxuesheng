#!/usr/bin/env python3
from pathlib import Path
cfg = Path("/home/flash/vela-p4/nuttx/.config")
for line in cfg.read_text().splitlines():
    if "USRSOCK" in line or "RPMSG" in line and line.startswith("CONFIG_"):
        print(line)
print("--- usrsock_rpmsg.h head ---")
p = Path("/home/flash/vela-p4/nuttx/include/nuttx/usrsock/usrsock_rpmsg.h")
print("exists", p.exists())
if p.exists():
    print("\n".join(p.read_text().splitlines()[:40]))
print("--- drivers_initialize around usrsock ---")
di = Path("/home/flash/vela-p4/nuttx/drivers/drivers_initialize.c").read_text().splitlines()
for i, line in enumerate(di, 1):
    if "usrsock" in line.lower() or "CONFIG_NET" in line:
        print(f"{i}:{line}")
