#!/usr/bin/env python3
"""Ensure SIMPLE_BOOT and LOG_LEVEL exist in the live .config, then overlay sources."""
from pathlib import Path
cfg = Path("/home/flash/vela-p4/nuttx/.config")
text = cfg.read_text()
need = [
    "CONFIG_ESPRESSIF_SIMPLE_BOOT=y\n",
    "CONFIG_ESPRESSIF_LOG_LEVEL=1\n",
]
for line in need:
    key = line.split("=")[0]
    if key + "=" not in text and f"# {key} is not set" not in text:
        text += line
    elif f"# {key} is not set" in text:
        text = text.replace(f"# {key} is not set\n", line)
    elif f"{key}=" not in text:
        text += line
cfg.write_text(text)
print("config patched")
k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Kconfig")
kt = k.read_text()
if "config ESPRESSIF_SIMPLE_BOOT" not in kt:
    kt += """
config ESPRESSIF_SIMPLE_BOOT
	bool
	default y
"""
    k.write_text(kt)
    print("kconfig SIMPLE_BOOT added")
