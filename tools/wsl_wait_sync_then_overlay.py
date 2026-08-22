#!/usr/bin/env python3
"""Wait for repo sync PID 485, then apply the ESP32-P4 overlay."""
import os
import subprocess
import time
from pathlib import Path

PID = 485
ROOT = Path("/home/flash/openvela")
LOG = ROOT / "overlay_wait.log"
SCRIPT = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/apply_esp32p4_overlay.py"
)


def log(msg: str) -> None:
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    with LOG.open("a", encoding="utf-8") as fh:
        fh.write(line + "\n")


def main() -> int:
    log("waiting for repo sync pid %s" % PID)
    while os.path.exists("/proc/%s" % PID):
        time.sleep(30)
    log("sync pid gone")
    if not (ROOT / "nuttx").is_dir():
        log("ERROR: nuttx missing after sync")
        return 1
    log("running overlay")
    rc = subprocess.call(["python3", str(SCRIPT)])
    log("overlay rc=%s" % rc)
    marker = ROOT / "overlay_done.txt"
    marker.write_text("rc=%s\n" % rc, encoding="utf-8")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
