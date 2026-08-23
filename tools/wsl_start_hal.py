#!/usr/bin/env python3
import subprocess
from pathlib import Path
script = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/wsl_clone_esp_hal.py"
)
log = Path("/home/flash/vela-p4/hal_clone.log")
cmd = "nohup python3 %s > %s 2>&1 & echo $!" % (script, log)
proc = subprocess.run(["bash", "-lc", cmd], check=True, capture_output=True, text=True)
print("HAL_PID", proc.stdout.strip())
