#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path

root = Path(os.environ.get("OPENVELA_ROOT", Path.home() / "openvela")).expanduser()
script = Path(__file__).with_name("wsl_wait_sync_then_overlay.py")
log = root / "overlay_wait.log"
pid = int((root / "repo_sync.pid").read_text(encoding="ascii").strip())
env = os.environ.copy()
env["PYTHONUNBUFFERED"] = "1"
with log.open("ab") as output:
    proc = subprocess.Popen(
        ["python3", str(script), str(pid), "--root", str(root)],
        stdin=subprocess.DEVNULL,
        stdout=output,
        stderr=subprocess.STDOUT,
        start_new_session=True,
        env=env,
    )
print("WAIT_PID", proc.pid)
