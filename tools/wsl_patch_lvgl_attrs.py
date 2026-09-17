#!/usr/bin/env python3
from pathlib import Path
import os

p = Path(os.environ.get("OPENVELA_ROOT", "/home/flash/openvela")) / "apps/graphics/lvgl/Makefile"
t = p.read_text(encoding="utf-8")
marker = "LVGL 9.2.1 empty Kconfig attribute compatibility"
if marker in t:
    print("already patched")
else:
    anchor = "include $(APPDIR)/Make.defs\n"
    compat = (
        f"\n# {marker}\n"
        'CFLAGS += "-DLV_ATTRIBUTE_MEM_ALIGN="\n'
        'CFLAGS += "-DLV_ATTRIBUTE_LARGE_CONST="\n'
    )
    if anchor not in t:
        raise SystemExit(f"anchor missing in {p}")
    p.write_text(t.replace(anchor, anchor + compat, 1), encoding="utf-8")
    print("patched", p)
