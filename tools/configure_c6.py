#!/usr/bin/env python3
"""Resolve optional Function-EV C6 diagnostic configuration; never flash."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--configure", action="store_true")
    args = parser.parse_args()
    tree = args.workspace.resolve()
    config = tree / "nuttx/.config"
    for relative in ("nuttx/arch/risc-v/src/esp32p4/esp32p4_sdmmc.c",
                     "apps/system/c6probe/Kconfig", "nuttx/.config"):
        if not (tree / relative).is_file():
            parser.error(f"Missing {relative}; apply C6 overlay first")
    if not args.configure:
        print("Source preflight passed. Use --configure to resolve Kconfig.")
        return
    env = os.environ.copy()
    env["PATH"] = f"{args.toolchain}:{Path.home() / '.local/bin'}:" + env["PATH"]
    env["CROSSDEV"] = "riscv32-esp-elf-"
    def run(command, cwd=None):
        subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)
    # Refresh generated app menus so the new command becomes visible.
    for relative in ("apps/Kconfig", "apps/system/Kconfig"):
        (tree / relative).unlink(missing_ok=True)
    run(["make", "-C", tree / "apps", "preconfig", f"TOPDIR={tree / 'nuttx'}"])
    for name in ("ESP32P4_SDMMC", "SYSTEM_C6PROBE", "NET", "NET_ETHERNET",
                 "NET_ARP", "NET_IPv4", "NET_TCP", "NET_UDP", "NET_SOCKOPTS",
                 "LIBC_NETDB", "NETDB_DNSCLIENT", "NETUTILS_DHCPC"):
        run(["kconfig-tweak", "--file", config, "--enable", name])
    # openvela's CMD53 helper calls DMA setup even for short byte transfers.
    # Without SDIO_DMA it returns ENOSYS, which the helper currently ignores.
    run(["kconfig-tweak", "--file", config, "--enable", "ESP32P4_SDMMC_DMA"])
    run(["kconfig-tweak", "--file", config, "--set-val", "MMCSD_MULTIBLOCK_LIMIT", "128"])
    run(["make", "olddefconfig"], tree / "nuttx")
    lines = set(config.read_text().splitlines())
    if "CONFIG_MMCSD_MULTIBLOCK_LIMIT=128" not in lines:
        raise RuntimeError("SDMMC multi-block limit must resolve to 128")
    for name in ("ESP32P4_SDMMC", "ESP32P4_SDMMC_DMA", "SDIO_DMA",
                 "SYSTEM_C6PROBE", "NET_ETHERNET"):
        if f"CONFIG_{name}=y" not in lines:
            raise RuntimeError(f"Required option did not resolve: {name}")
    for name, pin in (("CMD",19),("CLK",18),("D0",14),("D1",15),("D2",16),("D3",17)):
        if f"CONFIG_ESP32P4_SDMMC_{name}={pin}" not in lines:
            raise RuntimeError(f"Unexpected Function-EV pin: {name}")
    print("PASS: C6/SDIO configuration resolved; no radio command executed")


if __name__ == "__main__":
    main()
