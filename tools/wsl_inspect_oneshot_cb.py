#!/usr/bin/env python3
from pathlib import Path
oneshot = Path("/home/flash/vela-p4/nuttx/include/nuttx/timers/oneshot.h").read_text(encoding="utf-8", errors="replace")
for name in ("oneshot_process_callback", "oneshot_count_init", "CONFIG_ONESHOT"):
    print(name, oneshot.count(name))
i = oneshot.find("oneshot_process_callback")
print("==== process_callback context ====")
print(oneshot[max(0,i-400):i+600])

# Apache original for ESP32P4 in apply script
print("==== apply overlay block ====")
print(Path("/mnt/c/Users/flash/Desktop/openvela-contest/contest2026_359_dengfengzaojidecuipidaxuesheng/tools/apply_esp32p4_overlay.py").read_text(encoding="utf-8").split("CHIP_BLOCK")[0][:2500])
