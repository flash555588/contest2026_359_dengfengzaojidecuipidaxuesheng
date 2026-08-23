#!/usr/bin/env python3
from pathlib import Path
mp = Path("/home/flash/vela-p4/nuttx/nuttx.map")
text = mp.read_text(encoding="utf-8", errors="replace")
for name in ("memset", "memcpy", "memmove", "abort", "esp_cpu_intr",
             "efuse_hal_chip_revision", "_regi2c_ctrl_ll_master_enable_clock",
             "regi2c_ctrl_ll_master_configure_clock", "bootloader_init_mspi_clock",
             "cache_hal_init", "mmu_hal_init", "bootloader_print_banner"):
    print("====", name)
    n=0
    for line in text.splitlines():
        toks = line.split()
        # prefer definition lines with address then symbol
        if name in line and "0x" in line:
            # skip .group
            if ".group" in line:
                continue
            print(line[:230])
            n+=1
            if n>=4:
                break
    if n==0:
        print("NOT FOUND")

# specifically find .text.bootloader_init address
print("==== grep .text.bootloader_init ====")
prev=""
for line in text.splitlines():
    if ".text.bootloader_init" in line or (prev.startswith(" .text.bootloader_init") and "0x" in line):
        print(prev[:200])
        print(line[:230])
    prev=line
