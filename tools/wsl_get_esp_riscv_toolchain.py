#!/usr/bin/env python3
"""Download Linux riscv32-esp-elf from Espressif CN mirror and kill xpack curl."""
import hashlib
import os
import signal
import subprocess
from pathlib import Path

ROOT = Path(os.environ.get("OPENVELA_ROOT", Path.home() / "vela-p4")).expanduser()
URL = (
    "https://dl.espressif.cn/github_assets/espressif/crosstool-NG/releases/"
    "download/esp-15.2.0_20251204/"
    "riscv32-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz"
)
TGZ = ROOT / "riscv32-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz"
DEST = ROOT / "riscv32-esp-elf"
GCC = DEST / "bin" / "riscv32-esp-elf-gcc"
SHA256 = "ace5aae6afe98f754947be043d40173e2e22ace57754b11a394b7238eefa01cf"


def verify_archive() -> None:
    digest = hashlib.sha256()
    with TGZ.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != SHA256:
        raise SystemExit(f"SHA-256 mismatch for {TGZ}: {actual}")

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
    verify_archive()
    DEST.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(["tar", "-C", str(DEST), "--strip-components=1", "-xf", str(TGZ)])
subprocess.check_call([str(GCC), "--version"])
print("TOOLCHAIN_OK", GCC)
