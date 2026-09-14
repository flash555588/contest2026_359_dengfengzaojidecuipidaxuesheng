#!/usr/bin/env python3
"""Bootstrap a minimal NuttX+apps tree for ESP32-P4 while full repo sync continues."""
from __future__ import annotations

import os
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("OPENVELA_ROOT", Path.home() / "vela-p4")).expanduser()
NUTTX = ROOT / "nuttx"
APPS = ROOT / "apps"
VENDOR_ESP = ROOT / "vendor" / "espressif"
TOOL_TGZ = ROOT / "xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz"
TOOL_DIR = ROOT / "xpack-riscv-none-elf-gcc-15.2.0-1"
TOOL_URL = (
    "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/"
    "releases/download/v15.2.0-1/xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz"
)
TOOL_SHA256 = "aaaa8060c914851a3e5ee1ba82cc3d6f80972f90638a05c6e823a37557a33758"
OVERLAY = Path(__file__).with_name("apply_esp32p4_overlay.py")
SRC_VENDOR = Path(os.environ.get(
    "OPENVELA_SOURCE_ROOT", Path.home() / "openvela"
)).expanduser() / "vendor" / "espressif"


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    subprocess.check_call(cmd, **kw)


def verify_sha256(path: Path, expected: str) -> None:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != expected:
        raise SystemExit(f"SHA-256 mismatch for {path}: {actual}")


def clone(url: str, dest: Path, branch: str) -> None:
    if (dest / ".git").exists() and any(dest.iterdir()):
        print("exists", dest, flush=True)
        return
    if dest.exists():
        shutil.rmtree(dest)
    run(
        [
            "git",
            "clone",
            "--depth",
            "1",
            "--branch",
            branch,
            url,
            str(dest),
        ]
    )


def main() -> int:
    ROOT.mkdir(parents=True, exist_ok=True)
    os.chdir(ROOT)
    clone("https://github.com/open-vela/nuttx.git", NUTTX, "dev-ai-contest-2026")
    clone("https://github.com/open-vela/nuttx-apps.git", APPS, "dev-ai-contest-2026")

    VENDOR_ESP.parent.mkdir(parents=True, exist_ok=True)
    if not VENDOR_ESP.exists():
        if not SRC_VENDOR.exists():
            raise SystemExit("vendor/espressif not ready in /home/flash/openvela")
        VENDOR_ESP.symlink_to(SRC_VENDOR)
        print("link vendor/espressif", flush=True)

    if not (TOOL_DIR / "bin" / "riscv-none-elf-gcc").exists():
        if not TOOL_TGZ.exists() or TOOL_TGZ.stat().st_size < 10_000_000:
            run(["curl", "-L", "--fail", "-o", str(TOOL_TGZ), TOOL_URL])
        verify_sha256(TOOL_TGZ, TOOL_SHA256)
        run(["tar", "-C", str(ROOT), "-xf", str(TOOL_TGZ)])
    gcc = TOOL_DIR / "bin" / "riscv-none-elf-gcc"
    run([str(gcc), "--version"])

    env = os.environ.copy()
    env["OPENVELA_ROOT"] = str(ROOT)
    env["PATH"] = str(TOOL_DIR / "bin") + ":" + env.get("PATH", "")
    print("running overlay", flush=True)
    subprocess.check_call(["python3", str(OVERLAY)], env=env)

    marker = ROOT / "bootstrap_ok.txt"
    marker.write_text("ok\n", encoding="utf-8")
    print("BOOTSTRAP_OK", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
