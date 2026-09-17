#!/usr/bin/env python3
"""Wait for a recorded repo-sync process, then apply the overlay."""
import argparse
import os
import subprocess
import time
from pathlib import Path

def log(path: Path, msg: str) -> None:
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    with path.open("a", encoding="utf-8") as fh:
        fh.write(line + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pid", type=int)
    parser.add_argument("--root", type=Path, default=Path.home() / "openvela")
    args = parser.parse_args()
    root = args.root.expanduser().resolve()
    output = root / "overlay_wait.log"
    result = root / "repo_sync.rc"
    script = Path(__file__).with_name("apply_esp32p4_overlay.py")
    cmdline = Path(f"/proc/{args.pid}/cmdline")
    if not cmdline.is_file() or b"repo sync" not in cmdline.read_bytes().replace(b"\x00", b" "):
        log(output, f"ERROR: pid {args.pid} is not the recorded repo sync")
        return 1
    log(output, "waiting for repo sync pid %s" % args.pid)
    while os.path.exists("/proc/%s" % args.pid):
        time.sleep(30)
    log(output, "sync pid gone")
    if not result.is_file() or result.read_text(encoding="ascii").strip() != "0":
        log(output, "ERROR: repo sync did not report success")
        return 1
    if not (root / "nuttx").is_dir():
        log(output, "ERROR: nuttx missing after sync")
        return 1
    log(output, "running overlay")
    env = os.environ.copy()
    env["OPENVELA_ROOT"] = str(root)
    rc = subprocess.call(["python3", str(script)], env=env)
    log(output, "overlay rc=%s" % rc)
    marker = root / "overlay_done.txt"
    marker.write_text("rc=%s\n" % rc, encoding="utf-8")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
