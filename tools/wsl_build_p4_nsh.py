#!/usr/bin/env python3
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path("/home/flash/vela-p4")
GCC = ROOT / "riscv32-esp-elf/bin/riscv32-esp-elf-gcc"
NUTTX = ROOT / "nuttx"
CONFIG_NAME = sys.argv[1] if len(sys.argv) > 1 else "nsh"
if CONFIG_NAME not in {"nsh", "nsh-v3"}:
    raise SystemExit(f"unsupported config: {CONFIG_NAME}")

LOG = ROOT / f"{CONFIG_NAME}_build.log"
CFG = (
    "../vendor/espressif/boards/esp32p4/esp32p4-function-ev-board/configs/"
    + CONFIG_NAME
)

for _ in range(60):
    if GCC.exists():
        break
    time.sleep(2)
else:
    raise SystemExit("toolchain gcc missing")

env = os.environ.copy()
env["PATH"] = (
    str(Path.home() / ".local/bin")
    + ":"
    + str(GCC.parent)
    + ":"
    + env.get("PATH", "")
)
env["CROSSDEV"] = "riscv32-esp-elf-"
env["PYTHONUNBUFFERED"] = "1"

def run(cmd):
    print("+", " ".join(cmd), flush=True)
    with LOG.open("a", encoding="utf-8") as fh:
        fh.write("+ " + " ".join(cmd) + "\n")
        proc = subprocess.Popen(
            cmd, cwd=str(NUTTX), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        for line in proc.stdout:
            print(line, end="", flush=True)
            fh.write(line)
        rc = proc.wait()
    if rc != 0:
        raise SystemExit(rc)

LOG.write_text("", encoding="utf-8")
run([str(GCC), "--version"])

# The local overlay's PSRAM speed choice is sourced once per Espressif
# architecture. Give it a stable name so kconfiglib merges those definitions
# instead of treating the speed symbols as prompts outside an anonymous
# choice.
kconfig = NUTTX / "arch/risc-v/src/common/espressif/Kconfig"
ktext = kconfig.read_text(encoding="utf-8")
anonymous_choice = "if ESPRESSIF_SPIRAM\nchoice\n\tprompt \"PSRAM clock speed\""
named_choice = (
    "if ESPRESSIF_SPIRAM\nchoice ESPRESSIF_SPIRAM_SPEED\n"
    "\tprompt \"PSRAM clock speed\""
)
if anonymous_choice in ktext:
    kconfig.write_text(
        ktext.replace(anonymous_choice, named_choice, 1), encoding="utf-8"
    )
    print("Kconfig: named ESPRESSIF_SPIRAM_SPEED choice", flush=True)

# -E distclean needs a fully configured tree; wipe leftover config instead.
for rel in (".config", ".config.old", "Make.defs"):
    p = NUTTX / rel
    if p.exists():
        p.unlink()
chip_link = NUTTX / "arch/risc-v/src/chip"
if chip_link.is_symlink() or chip_link.exists():
    chip_link.unlink()
run(["bash", "./tools/configure.sh", "-l", CFG])
run(["make", "-j2"])
print("BUILD_OK", flush=True)
