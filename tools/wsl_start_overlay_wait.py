#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path

script = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/wsl_wait_sync_then_overlay.py"
)
log = Path("/home/flash/openvela/overlay_wait.log")
env = os.environ.copy()
env["PYTHONUNBUFFERED"] = "1"
cmd = "nohup python3 %s >> %s 2>&1 & echo $!" % (script, log)
proc = subprocess.run(["bash", "-lc", cmd], check=True, capture_output=True, text=True, env=env)
print("WAIT_PID", proc.stdout.strip())
