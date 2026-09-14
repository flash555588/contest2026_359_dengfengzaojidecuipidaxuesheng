#!/usr/bin/env python3
"""Resume xpack download, apply overlay, print toolchain/Kconfig status."""
import hashlib
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("OPENVELA_ROOT", Path.home() / "vela-p4")).expanduser()
TOOL_TGZ = ROOT / "xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz"
TOOL_DIR = ROOT / "xpack-riscv-none-elf-gcc-15.2.0-1"
TOOL_URL = (
    "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/"
    "releases/download/v15.2.0-1/xpack-riscv-none-elf-gcc-15.2.0-1-linux-x64.tar.gz"
)
TOOL_SHA256 = "aaaa8060c914851a3e5ee1ba82cc3d6f80972f90638a05c6e823a37557a33758"
OVERLAY = Path(__file__).with_name("apply_esp32p4_overlay.py")


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.check_call(cmd)


def verify_archive() -> None:
    digest = hashlib.sha256()
    with TOOL_TGZ.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != TOOL_SHA256:
        raise SystemExit(f"SHA-256 mismatch for {TOOL_TGZ}: {actual}")


def main() -> int:
    gcc = TOOL_DIR / "bin" / "riscv-none-elf-gcc"
    if not gcc.exists():
        run(
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
                "2",
                "-o",
                str(TOOL_TGZ),
                TOOL_URL,
            ]
        )
        verify_archive()
        run(["tar", "-C", str(ROOT), "-xf", str(TOOL_TGZ)])
    run([str(gcc), "--version"])

    env = os.environ.copy()
    env["OPENVELA_ROOT"] = str(ROOT)
    env["PATH"] = str(TOOL_DIR / "bin") + ":" + env.get("PATH", "")
    subprocess.check_call(["python3", str(OVERLAY)], env=env)
    (ROOT / "bootstrap_ok.txt").write_text("ok\n", encoding="utf-8")
    print("BOOTSTRAP_OK", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
