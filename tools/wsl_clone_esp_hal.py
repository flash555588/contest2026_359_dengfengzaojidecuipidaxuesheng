#!/usr/bin/env python3
"""Replace chip symlink with a real copy, then clone esp-hal-3rdparty with retries."""
from __future__ import annotations

import os
import shutil
import subprocess
import time
from pathlib import Path

NUTTX = Path("/home/flash/vela-p4/nuttx")
CHIP = NUTTX / "arch/risc-v/src/esp32p4"
SRC = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/chip/esp32p4/src"
)
HAL = CHIP / "esp-hal-3rdparty"
COMMIT = "8d0a898910084206721a0892ab093021bca1496a"
URLS = [
    "https://github.com/espressif/esp-hal-3rdparty.git",
    "https://gitclone.com/github.com/espressif/esp-hal-3rdparty.git",
]


def run(cmd, cwd=None):
    print("+", " ".join(cmd), flush=True)
    subprocess.check_call(cmd, cwd=cwd)


def materialize_chip() -> None:
    if CHIP.is_symlink():
        print("replacing chip symlink with copy", flush=True)
        CHIP.unlink()
        shutil.copytree(SRC, CHIP, symlinks=True)
    elif not CHIP.exists():
        shutil.copytree(SRC, CHIP, symlinks=True)


def clone_hal() -> None:
    if (HAL / ".git").exists():
        try:
            head = subprocess.check_output(
                ["git", "-C", str(HAL), "rev-parse", "HEAD"], text=True
            ).strip()
            if head.startswith(COMMIT[:12]) or COMMIT.startswith(head[:12]):
                print("HAL already at", head, flush=True)
                return
        except subprocess.CalledProcessError:
            pass
        shutil.rmtree(HAL)

    last = None
    for url in URLS:
        for attempt in range(1, 6):
            try:
                if HAL.exists():
                    shutil.rmtree(HAL)
                HAL.mkdir(parents=True)
                run(["git", "init"], cwd=HAL)
                run(["git", "remote", "add", "origin", url], cwd=HAL)
                run(
                    [
                        "git",
                        "-c",
                        "http.version=HTTP/1.1",
                        "fetch",
                        "--depth",
                        "1",
                        "origin",
                        COMMIT,
                    ],
                    cwd=HAL,
                )
                run(["git", "checkout", "--force", "FETCH_HEAD"], cwd=HAL)
                print("HAL_OK", url, "attempt", attempt, flush=True)
                return
            except subprocess.CalledProcessError as e:
                last = e
                print("fail", url, attempt, e, flush=True)
                time.sleep(2 * attempt)
    raise SystemExit(f"HAL clone failed: {last}")


def main() -> int:
    materialize_chip()
    clone_hal()
    # recreate chip symlink used by NuttX dirlinks if missing
    link = NUTTX / "arch/risc-v/src/chip"
    if not link.exists():
        link.symlink_to(CHIP)
    print("CHIP_HAL_READY", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
