#!/usr/bin/env python3
"""Download Linux riscv32-esp-elf from Espressif CN mirror and kill xpack curl."""
import os
import signal
import subprocess
from pathlib import Path

ROOT = Path("/home/flash/vela-p4")
URL = (
    "https://dl.espressif.cn/github_assets/espressif/crosstool-NG/releases/"
    "download/esp-15.2.0_20251204/"
    "riscv32-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz"
)
TGZ = ROOT / "riscv32-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz"
DEST = ROOT / "riscv32-esp-elf"
GCC = DEST / "bin" / "riscv32-esp-elf-gcc"

# stop flaky xpack curl
for proc in Path("/proc").iterdir():
    if not proc.name.isdigit():
        continue
    try:
        cmd = (proc / "cmdline").read_bytes()
    except Exception:
        continue
    if b"xpack-riscv-none-elf-gcc" in cmd or b"wsl_resume_toolchain_overlay" in cmd:
        os.kill(int(proc.name), signal.SIGTERM)
        print("killed", proc.name)

if not GCC.exists():
    ROOT.mkdir(parents=True, exist_ok=True)
    print("downloading", URL, flush=True)
    subprocess.check_call(
        [
            "curl",
            "-L",
            "--fail",
            "-C",
            "-",
            "--retry",
            "8",
            "--retry-all-errors",
            "--retry-delay",
            "3",
            "-o",
            str(TGZ),
            URL,
        ]
    )
    DEST.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(["tar", "-C", str(DEST), "--strip-components=1", "-xf", str(TGZ)])
subprocess.check_call([str(GCC), "--version"])
print("TOOLCHAIN_OK", GCC)
