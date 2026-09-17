#!/usr/bin/env python3
import subprocess, os
from pathlib import Path
cfg = Path("/home/flash/vela-p4/nuttx/.config").read_text()
for key in ("CONFIG_ESPRESSIF_SIMPLE_BOOT", "CONFIG_ESPRESSIF_LOG_LEVEL", "CONFIG_ESPRESSIF_FLASH_16M"):
    for line in cfg.splitlines():
        if key in line:
            print(line)
print("--- esptool ---")
print(subprocess.check_output(["bash","-lc","which esptool.py || which esptool || pip3 show esptool | head -2"], text=True, stderr=subprocess.STDOUT))
