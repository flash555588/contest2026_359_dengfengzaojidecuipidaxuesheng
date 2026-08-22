#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path

script = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/wsl_bootstrap_p4_min.py"
)
log = Path("/home/flash/vela-p4/bootstrap.log")
log.parent.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env["PYTHONUNBUFFERED"] = "1"
cmd = "nohup python3 %s > %s 2>&1 & echo $!" % (script, log)
proc = subprocess.run(["bash", "-lc", cmd], check=True, capture_output=True, text=True, env=env)
print("BOOTSTRAP_PID", proc.stdout.strip())
print("LOG", log)
