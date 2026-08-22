#!/usr/bin/env python3
"""Initialize openvela contest workspace on WSL ext4."""
import os
import subprocess
import sys

ROOT = "/home/flash/openvela"
os.makedirs(ROOT, exist_ok=True)
os.chdir(ROOT)
subprocess.check_call(["git", "lfs", "install", "--skip-repo"])
if os.path.isdir(".repo"):
    print("INFO: .repo already exists, skip repo init")
else:
    subprocess.check_call(
        [
            "repo",
            "init",
            "-u",
            "https://github.com/open-vela/contest2026_359_dengfengzaojidecuipidaxuesheng",
            "-b",
            "dev-ai-contest-2026",
            "-m",
            "contest2026_359_dengfengzaojidecuipidaxuesheng.xml",
            "--git-lfs",
        ]
    )
print("INIT_OK", os.getcwd())
sys.exit(0)
