#!/usr/bin/env python3
"""C23 removed ATOMIC_VAR_INIT; GCC 15 treats it as undeclared."""
from pathlib import Path
import re

root = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
n = 0
for path in root.rglob("*.c"):
    text = path.read_text(encoding="utf-8", errors="replace")
    if "ATOMIC_VAR_INIT" not in text:
        continue
    new = re.sub(r"ATOMIC_VAR_INIT\((.*)\)", r"(\1)", text)
    if new != text:
        path.write_text(new, encoding="utf-8")
        n += 1
        print("patched", path)
print("files", n)
