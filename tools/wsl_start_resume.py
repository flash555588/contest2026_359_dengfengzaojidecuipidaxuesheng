#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path

script = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/wsl_resume_toolchain_overlay.py"
)
log = Path("/home/flash/vela-p4/bootstrap.log")
cmd = "nohup python3 %s >> %s 2>&1 & echo $!" % (script, log)
proc = subprocess.run(["bash", "-lc", cmd], check=True, capture_output=True, text=True)
print("RESUME_PID", proc.stdout.strip())
