#!/usr/bin/env python3
"""ESP32-P4 CLINT mtimer uses the oneshot COUNT ABI.

Do not select CONFIG_ONESHOT (that pulls GPTimer /dev/oneshot via board
bringup). Only CONFIG_ONESHOT_COUNT is required for riscv_mtimer.c.
"""
from pathlib import Path

KCONFIG = Path("/home/flash/vela-p4/nuttx/arch/risc-v/Kconfig")
DOTCONFIG = Path("/home/flash/vela-p4/nuttx/.config")
CONFIG_H = Path("/home/flash/vela-p4/nuttx/include/nuttx/config.h")

k = KCONFIG.read_text(encoding="utf-8")
old = "\tselect ONESHOT_COUNT if ONESHOT\n"
new = "\tselect ONESHOT_COUNT\n"
if old in k:
    # Only rewrite the ESP32-P4 block.
    start = k.find("config ARCH_CHIP_ESP32P4")
    if start < 0:
        raise SystemExit("ARCH_CHIP_ESP32P4 missing")
    end = k.find("\nconfig ARCH_CHIP_", start + 1)
    block = k[start:end]
    if old not in block:
        raise SystemExit("ONESHOT_COUNT if ONESHOT not in ESP32P4 block")
    k = k[:start] + block.replace(old, new, 1) + k[end:]
    KCONFIG.write_text(k, encoding="utf-8")
    print("Kconfig: select ONESHOT_COUNT")
elif "config ARCH_CHIP_ESP32P4" in k and "select ONESHOT_COUNT\n" in k[k.find("config ARCH_CHIP_ESP32P4"):]:
    print("Kconfig already selects ONESHOT_COUNT")
else:
    raise SystemExit("could not patch ARCH_CHIP_ESP32P4 ONESHOT_COUNT")

cfg = DOTCONFIG.read_text(encoding="utf-8")
if "CONFIG_ONESHOT_COUNT=y" not in cfg:
    if "# CONFIG_ONESHOT_COUNT is not set" in cfg:
        cfg = cfg.replace("# CONFIG_ONESHOT_COUNT is not set", "CONFIG_ONESHOT_COUNT=y", 1)
    else:
        cfg += "CONFIG_ONESHOT_COUNT=y\n"
    DOTCONFIG.write_text(cfg, encoding="utf-8")
    print(".config: CONFIG_ONESHOT_COUNT=y")
else:
    print(".config already has ONESHOT_COUNT")

ch = CONFIG_H.read_text(encoding="utf-8")
if "#define CONFIG_ONESHOT_COUNT 1" not in ch:
    if "/* CONFIG_ONESHOT_COUNT is not set */" in ch:
        ch = ch.replace(
            "/* CONFIG_ONESHOT_COUNT is not set */",
            "#define CONFIG_ONESHOT_COUNT 1",
            1,
        )
    else:
        ch = ch.replace(
            "#define CONFIG_ARCH_CHIP_ESP32P4 1",
            "#define CONFIG_ARCH_CHIP_ESP32P4 1\n#define CONFIG_ONESHOT_COUNT 1",
            1,
        )
    CONFIG_H.write_text(ch, encoding="utf-8")
    print("config.h: CONFIG_ONESHOT_COUNT 1")
else:
    print("config.h already has ONESHOT_COUNT")
