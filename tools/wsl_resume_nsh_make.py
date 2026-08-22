#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path

NUTTX = Path("/home/flash/vela-p4/nuttx")
GCCBIN = Path("/home/flash/vela-p4/riscv32-esp-elf/bin")
HAL = NUTTX / "arch/risc-v/src/esp32p4/esp-hal-3rdparty"
LOG = Path("/home/flash/vela-p4/nsh_build.log")

env = os.environ.copy()
env["PATH"] = (
    str(Path.home() / ".local/bin") + ":" + str(GCCBIN) + ":" + env.get("PATH", "")
)
env["CROSSDEV"] = "riscv32-esp-elf-"
env["PYTHONUNBUFFERED"] = "1"

# Submodules are optional for UART NSH; clone on demand if the build asks.

cmd = ["make", "-j2"]
print("+", " ".join(cmd), flush=True)
with LOG.open("a", encoding="utf-8") as fh:
    fh.write("\n+ make -j2 resume\n")
    proc = subprocess.Popen(
        cmd, cwd=str(NUTTX), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    for line in proc.stdout:
        print(line, end="", flush=True)
        fh.write(line)
    rc = proc.wait()
if rc == 0:
    subprocess.check_call(
        [
            "python3",
            "/mnt/c/Users/flash/Desktop/openvela-contest/"
            "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/wsl_copy_firmware.py",
        ]
    )
    print("FIRMWARE_COPIED", flush=True)
raise SystemExit(rc)
