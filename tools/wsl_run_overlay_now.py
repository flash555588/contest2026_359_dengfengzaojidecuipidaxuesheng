#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path

env = os.environ.copy()
env["OPENVELA_ROOT"] = "/home/flash/vela-p4"
script = (
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/apply_esp32p4_overlay.py"
)
print("Kconfig exists", Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig").exists())
print("common espressif", Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif").exists())
print("tools espressif", Path("/home/flash/vela-p4/nuttx/tools/espressif").exists())
print("esp32c3", Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32c3").exists())
print("esp32p4", Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4").exists())
rc = subprocess.call(["python3", script], env=env)
print("overlay rc", rc)
