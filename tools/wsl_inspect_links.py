#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4")
print("is_symlink", p.is_symlink())
print("resolve", p.resolve())
print("Make.defs exists", (p/"Make.defs").exists())
# board
b = Path("/home/flash/vela-p4/nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board")
print("board symlink", b.is_symlink(), b.resolve() if b.exists() else None)
print("libatomic.a?")
import os
for dirpath, _, files in os.walk("/home/flash/vela-p4/riscv32-esp-elf/lib"):
    for f in files:
        if f.endswith(".a") and ("atomic" in f or f=="libgcc.a"):
            print(Path(dirpath)/f)
