#!/usr/bin/env python3
from pathlib import Path
cfg = Path("/home/flash/vela-p4/nuttx/.config")
for line in cfg.read_text().splitlines():
    if "CONFIG_NET" in line or "USRSOCK" in line or "CONFIG_NET_USRSOCK" in line:
        if line.startswith("CONFIG_") or " is not set" in line:
            if "NET" in line or "USRSOCK" in line:
                print(line)
