#!/bin/bash
set -euo pipefail
cd /home/flash/vela-p4/nuttx
export PATH="/home/flash/.local/bin:/home/flash/vela-p4/riscv32-esp-elf/bin:$PATH"
export CROSSDEV=riscv32-esp-elf-
exec make -j2
