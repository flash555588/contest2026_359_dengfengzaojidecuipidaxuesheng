#!/usr/bin/env python3
import subprocess
from pathlib import Path
script = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/wsl_build_p4_nsh.py"
)
log = Path("/home/flash/vela-p4/nsh_build_outer.log")
cmd = "nohup python3 %s > %s 2>&1 & echo $!" % (script, log)
proc = subprocess.run(["bash", "-lc", cmd], check=True, capture_output=True, text=True)
print("BUILD_PID", proc.stdout.strip())
