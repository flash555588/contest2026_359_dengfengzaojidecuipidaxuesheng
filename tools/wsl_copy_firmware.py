#!/usr/bin/env python3
"""Copy WSL NuttX image to the Windows contest tree for COM7 flashing."""
from pathlib import Path
import shutil

src_dir = Path("/home/flash/vela-p4/nuttx")
dst = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/firmware/esp32p4-nsh"
)
dst.mkdir(parents=True, exist_ok=True)
copied = []
# nuttx.bin from `objcopy -O binary` is a sparse 64MB-hole dump (~500MB) and
# is not flashable. Keep ELF/map/hex; Windows IDF esptool elf2image produces
# the simple-boot image.
for name in ("nuttx.hex", "nuttx", "nuttx.map"):
    s = src_dir / name
    if s.exists() and s.is_file():
        shutil.copy2(s, dst / name)
        copied.append(f"{name} {s.stat().st_size}")
print("DST", dst)
print("COPIED", copied)
if not (dst / "nuttx").exists():
    raise SystemExit("nuttx ELF missing; build not finished")
