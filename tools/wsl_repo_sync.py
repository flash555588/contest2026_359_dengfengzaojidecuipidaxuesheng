#!/usr/bin/env python3
"""Start repo sync in background under /home/flash/openvela."""
import os
import subprocess

ROOT = "/home/flash/openvela"
LOG = os.path.join(ROOT, "repo_sync.log")
os.chdir(ROOT)
# 7.8GB RAM: keep concurrency modest
cmd = "nohup repo sync -c -j4 > %s 2>&1 & echo $!" % LOG
proc = subprocess.run(["bash", "-lc", cmd], check=True, capture_output=True, text=True)
print("PID", proc.stdout.strip())
print("LOG", LOG)
