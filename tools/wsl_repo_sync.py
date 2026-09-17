#!/usr/bin/env python3
"""Start repo sync in the background and persist its PID and result."""
import os
import subprocess
from pathlib import Path

ROOT = Path(os.environ.get("OPENVELA_ROOT", Path.home() / "openvela")).expanduser()
LOG = ROOT / "repo_sync.log"
PID_FILE = ROOT / "repo_sync.pid"
RC_FILE = ROOT / "repo_sync.rc"
ROOT.mkdir(parents=True, exist_ok=True)
RC_FILE.unlink(missing_ok=True)
# 7.8GB RAM: keep concurrency modest
command = 'repo sync -c -j4; rc=$?; printf "%s\\n" "$rc" > "$1"; exit "$rc"'
with LOG.open("ab") as output:
    proc = subprocess.Popen(
        ["bash", "-c", command, "repo-sync", str(RC_FILE)],
        cwd=ROOT,
        stdin=subprocess.DEVNULL,
        stdout=output,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
PID_FILE.write_text(f"{proc.pid}\n", encoding="ascii")
print("PID", proc.pid)
print("LOG", LOG)
print("PID_FILE", PID_FILE)
