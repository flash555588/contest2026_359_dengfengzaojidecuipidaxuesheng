#!/usr/bin/env python3
from pathlib import Path
cfg = Path("/home/flash/vela-p4/nuttx/.config").read_text()
for line in cfg.splitlines():
    if "FLASH" in line and not line.startswith("#"):
        print(line)
elf = Path("/home/flash/vela-p4/nuttx/nuttx")
binp = Path("/home/flash/vela-p4/nuttx/nuttx.bin")
print("elf", elf.exists(), elf.stat().st_size if elf.exists() else None)
print("bin", binp.exists(), binp.stat().st_size if binp.exists() else None)
# magic of current bin
if binp.exists():
    b = binp.read_bytes()[:16]
    print("bin head", b.hex())
