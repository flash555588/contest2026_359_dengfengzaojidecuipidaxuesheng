#!/usr/bin/env python3
import os
import shutil
from pathlib import Path

src = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/tools/kconfig-tweak"
)
dst = Path.home() / ".local/bin/kconfig-tweak"
dst.parent.mkdir(parents=True, exist_ok=True)
data = src.read_bytes().replace(b"\r\n", b"\n")
dst.write_bytes(data)
os.chmod(dst, 0o755)
print("installed", dst)
