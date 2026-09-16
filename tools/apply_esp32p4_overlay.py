#!/usr/bin/env python3
"""Wire ESP32-P4 board/chip overlays into a synced openvela tree.

Run on WSL after `repo sync` finishes:

    python3 tools/apply_esp32p4_overlay.py
"""
from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path

OPENVELA = Path(os.environ.get("OPENVELA_ROOT", "/home/flash/openvela"))
CONTEST_WIN = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng"
)
CONTEST_WSL = OPENVELA / "contest2026_359_dengfengzaojidecuipidaxuesheng"

ARCH_CHIP_BLOCK = """
config ARCH_CHIP_ESP32P4
	bool "ESP32-P4"
	select ARCH_RV32
	select ARCH_RV_ISA_M
	select ARCH_RV_ISA_A
	select ARCH_RV_ISA_C
	select ARCH_VECNOTIRQ
	select ARCH_HAVE_BOOTLOADER if !ESPRESSIF_SIMPLE_BOOT
	select ARCH_HAVE_I2CRESET
	select ARCH_HAVE_MPU
	select ARCH_HAVE_RESET
	select ARCH_HAVE_RNG
	select ARCH_HAVE_TICKLESS
	select ARCH_HAVE_MULTICPU
	select LIBC_ARCH_MEMCPY
	select LIBC_ARCH_MEMCHR
	select LIBC_ARCH_MEMCMP
	select LIBC_ARCH_MEMMOVE
	select LIBC_ARCH_MEMSET
	select LIBC_ARCH_STRCHR
	select LIBC_ARCH_STRCMP
	select LIBC_ARCH_STRCPY
	select LIBC_ARCH_STRLCPY
	select LIBC_ARCH_STRNCPY
	select LIBC_ARCH_STRLEN
	select LIBC_ARCH_STRNLEN
	select ARCH_HAVE_RAMFUNCS
	select ONESHOT_COUNT if ONESHOT
	select ARCH_MINIMAL_VECTORTABLE
	select ARCH_CHIP_ESPRESSIF
	---help---
		Espressif ESP32-P4 (RV32IMC).
"""

SOURCE_BLOCK = """if ARCH_CHIP_ESP32P4
source "arch/risc-v/src/esp32p4/Kconfig"
endif
"""


def log(msg: str) -> None:
    print(msg, flush=True)


def copy_overlay_file(source: str, destination: str) -> str:
    """Copy a file unless the destination already resolves to that file."""

    try:
        if os.path.exists(destination) and os.path.samefile(source, destination):
            return destination
    except OSError:
        pass

    return shutil.copy2(source, destination)


def contest_src() -> Path:
    if CONTEST_WIN.is_dir() and (CONTEST_WIN / "board" / "esp32p4-function-ev-board").is_dir():
        return CONTEST_WIN
    if CONTEST_WSL.is_dir() and (CONTEST_WSL / "board" / "esp32p4-function-ev-board").is_dir():
        return CONTEST_WSL
    raise SystemExit("contest board tree not found")


def rsync_contest(src: Path) -> Path:
    """Keep a native-ext4 copy for faster builds when source lives on /mnt/c."""
    if src == CONTEST_WSL:
        return CONTEST_WSL
    if not CONTEST_WSL.exists():
        log("WSL contest project not cloned yet; linking directly from /mnt/c")
        return src
    for rel in (
        "board/esp32p4-function-ev-board",
        "board/esp32p4-common",
        "chip/esp32p4",
        "contest2026_359_dengfengzaojidecuipidaxuesheng.xml",
        "tools/apply_esp32p4_overlay.py",
    ):
        s = src / rel
        d = CONTEST_WSL / rel
        if not s.exists():
            continue
        d.parent.mkdir(parents=True, exist_ok=True)
        if s.is_dir():
            if d.exists():
                shutil.rmtree(d)
            shutil.copytree(s, d, symlinks=True)
        else:
            shutil.copy2(s, d)
        log(f"rsync {rel}")
    return CONTEST_WSL


def ensure_symlink(src: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.is_symlink():
        current = Path(os.readlink(dest))
        if current == src or dest.resolve() == src.resolve():
            log(f"link ok {dest} -> {src}")
            return
        dest.unlink()
    elif dest.exists():
        log(f"SKIP existing path {dest}")
        return
    dest.symlink_to(src)
    log(f"link {dest} -> {src}")


def patch_arch_kconfig(kconfig: Path) -> None:
    text = kconfig.read_text(encoding="utf-8")
    if "config ARCH_CHIP_ESP32P4" not in text:
        needle = '\t---help---\n\t\tEspressif ESP32-H2 (RV32IMC).\n'
        alt = "config ARCH_CHIP_C906"
        if needle in text:
            text = text.replace(needle, needle + ARCH_CHIP_BLOCK, 1)
        elif alt in text:
            text = text.replace(alt, ARCH_CHIP_BLOCK + "\n" + alt, 1)
        else:
            raise SystemExit(f"cannot find insertion point in {kconfig}")
        log(f"patched ARCH_CHIP_ESP32P4 into {kconfig}")
    else:
        log("ARCH_CHIP_ESP32P4 already present")

    if 'default "esp32p4"' not in text:
        text = text.replace(
            '	default "esp32h2"               if ARCH_CHIP_ESP32H2\n',
            '	default "esp32h2"               if ARCH_CHIP_ESP32H2\n'
            '	default "esp32p4"               if ARCH_CHIP_ESP32P4\n',
            1,
        )
        log("patched ARCH_CHIP default string")

    if 'source "arch/risc-v/src/esp32p4/Kconfig"' not in text:
        text = text.replace(
            'if ARCH_CHIP_ESP32H2\nsource "arch/risc-v/src/esp32h2/Kconfig"\nendif\n',
            'if ARCH_CHIP_ESP32H2\nsource "arch/risc-v/src/esp32h2/Kconfig"\nendif\n'
            + SOURCE_BLOCK,
            1,
        )
        log("patched esp32p4 Kconfig source")

    if "select ARCH_CHIP_ESPRESSIF" not in text.split("config ARCH_CHIP_ESP32P4", 1)[-1][:800]:
        text = text.replace(
            "\tselect ARCH_MINIMAL_VECTORTABLE\n\t---help---\n\t\tEspressif ESP32-P4 (RV32IMC).\n",
            "\tselect ARCH_MINIMAL_VECTORTABLE\n"
            "\tselect ARCH_CHIP_ESPRESSIF\n"
            "\t---help---\n\t\tEspressif ESP32-P4 (RV32IMC).\n",
            1,
        )
        log("patched ARCH_CHIP_ESP32P4 to select ARCH_CHIP_ESPRESSIF")

    kconfig.write_text(text, encoding="utf-8")


def patch_usrsock_include(path: Path) -> None:
    """Contest nuttx includes usrsock headers even when CONFIG_NET is off."""
    if not path.is_file():
        return
    text = path.read_text(encoding="utf-8")
    old = "#include <nuttx/usrsock/usrsock_rpmsg.h>\n"
    new = (
        "#ifdef CONFIG_NET\n"
        "#  include <nuttx/usrsock/usrsock_rpmsg.h>\n"
        "#endif\n"
    )
    if old in text:
        path.write_text(text.replace(old, new, 1), encoding="utf-8")
        log(f"patched usrsock include in {path}")
    elif "usrsock_rpmsg.h" in text:
        log("usrsock include already guarded")
    else:
        log(f"SKIP usrsock include in {path}")


def maybe_patch_common_espressif(common: Path) -> None:
    k = common / "Kconfig"
    if not k.is_file():
        return
    text = k.read_text(encoding="utf-8")
    orig = text

    old_if = "if ARCH_CHIP_ESPRESSIF || ARCH_CHIP_ESP32C6 || ARCH_CHIP_ESP32H2 || ARCH_CHIP_ESP32C3_GENERIC"
    new_if = old_if + " || ARCH_CHIP_ESP32P4"
    if old_if in text and "ARCH_CHIP_ESP32P4" not in text[:400]:
        text = text.replace(old_if, new_if, 1)
        log("patched common/espressif if-guard")

    if "config ESPRESSIF_ESP32P4" not in text:
        text = text.replace(
            "config ESPRESSIF_ESP32H2\n"
            "\tbool \"ESP32-H2\"\n"
            "\tselect ARCH_HAVE_I2CRESET\n"
            "\t---help---\n"
            "\t\tEspressif ESP32-H2 (RV32IMC).\n"
            "\n"
            "endchoice # ESPRESSIF_CHIP_SERIES",
            "config ESPRESSIF_ESP32H2\n"
            "\tbool \"ESP32-H2\"\n"
            "\tselect ARCH_HAVE_I2CRESET\n"
            "\t---help---\n"
            "\t\tEspressif ESP32-H2 (RV32IMC).\n"
            "\n"
            "config ESPRESSIF_ESP32P4\n"
            "\tbool \"ESP32-P4\"\n"
            "\tdepends on ARCH_CHIP_ESP32P4\n"
            "\tselect ARCH_HAVE_I2CRESET\n"
            "\t---help---\n"
            "\t\tEspressif ESP32-P4 dual-core RV32IMC.\n"
            "\n"
            "endchoice # ESPRESSIF_CHIP_SERIES",
            1,
        )
        text = text.replace(
            'choice ESPRESSIF_CHIP_SERIES\n'
            '\tprompt "Chip Series"\n'
            '\tdefault ESPRESSIF_ESP32C3\n',
            'choice ESPRESSIF_CHIP_SERIES\n'
            '\tprompt "Chip Series"\n'
            '\tdefault ESPRESSIF_ESP32P4 if ARCH_CHIP_ESP32P4\n'
            '\tdefault ESPRESSIF_ESP32C3\n',
            1,
        )
        text = text.replace(
            '\tdefault "esp32h2" if ESPRESSIF_ESP32H2\n'
            '\tdefault "unknown"\n',
            '\tdefault "esp32h2" if ESPRESSIF_ESP32H2\n'
            '\tdefault "esp32p4" if ESPRESSIF_ESP32P4\n'
            '\tdefault "unknown"\n',
            1,
        )
        text = text.replace(
            "\tdefault 1 if ESPRESSIF_ESP32C3 || ESPRESSIF_ESP32C6 || ESPRESSIF_ESP32H2\n",
            "\tdefault 2 if ESPRESSIF_ESP32P4\n"
            "\tdefault 1 if ESPRESSIF_ESP32C3 || ESPRESSIF_ESP32C6 || ESPRESSIF_ESP32H2\n",
            1,
        )
        log("patched ESPRESSIF_ESP32P4 series choice")

    mk = common / "Make.defs"
    if mk.is_file():
        mktext = mk.read_text(encoding="utf-8")
        marker = "ifeq ($(CONFIG_ARCH_CHIP_ESP32P4),y)"
        if marker not in mktext:
            mktext = mktext.replace(
                "ESP_HAL_3RDPARTY_REPO   = esp-hal-3rdparty\n"
                "ifndef ESP_HAL_3RDPARTY_VERSION\n",
                "ESP_HAL_3RDPARTY_REPO   = esp-hal-3rdparty\n"
                "ifeq ($(CONFIG_ARCH_CHIP_ESP32P4),y)\n"
                "\tESP_HAL_3RDPARTY_VERSION = 8d0a898910084206721a0892ab093021bca1496a\n"
                "endif\n"
                "ifndef ESP_HAL_3RDPARTY_VERSION\n",
                1,
            )
            mk.write_text(mktext, encoding="utf-8")
            log("patched HAL commit for ESP32-P4")

    if text != orig:
        k.write_text(text, encoding="utf-8")


def main() -> int:
    if not (OPENVELA / "nuttx").is_dir():
        raise SystemExit(f"openvela nuttx missing under {OPENVELA} (repo sync not finished?)")
    src = rsync_contest(contest_src())
    links = [
        (
            src / "board/esp32p4-function-ev-board",
            OPENVELA / "vendor/espressif/boards/esp32p4/esp32p4-function-ev-board",
        ),
        (
            src / "board/esp32p4-common",
            OPENVELA / "nuttx/boards/risc-v/esp32p4/common",
        ),
        (
            src / "chip/esp32p4/src",
            OPENVELA / "nuttx/arch/risc-v/src/esp32p4",
        ),
        (
            src / "chip/esp32p4/include",
            OPENVELA / "nuttx/arch/risc-v/include/esp32p4",
        ),
    ]
    common_dest = OPENVELA / "nuttx/arch/risc-v/src/common/espressif"
    tools_dest = OPENVELA / "nuttx/tools/espressif"
    if not common_dest.exists():
        links.append((src / "chip/esp32p4/common-espressif", common_dest))
    else:
        # A bootstrap tree already contains the upstream common/espressif
        # directory, so a repo <linkfile> cannot replace it with a symlink.
        # Overlay the contest copy in place to keep local WSL builds identical
        # to manifest-based builds while preserving any unrelated upstream
        # files that the reduced contest snapshot does not carry.
        shutil.copytree(
            src / "chip/esp32p4/common-espressif",
            common_dest,
            dirs_exist_ok=True,
            symlinks=True,
            copy_function=copy_overlay_file,
        )
        log(f"overlay {src / 'chip/esp32p4/common-espressif'} -> {common_dest}")
        maybe_patch_common_espressif(common_dest)
    if not tools_dest.exists():
        links.append((src / "chip/esp32p4/tools-espressif", tools_dest))
    else:
        log(f"keep existing {tools_dest}")

    for s, d in links:
        if not s.exists():
            raise SystemExit(f"missing overlay source {s}")
        ensure_symlink(s, d)

    patch_arch_kconfig(OPENVELA / "nuttx/arch/risc-v/Kconfig")
    patch_usrsock_include(OPENVELA / "nuttx/drivers/drivers_initialize.c")
    log("OVERLAY_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
