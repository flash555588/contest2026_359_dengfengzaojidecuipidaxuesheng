#!/usr/bin/env python3
from pathlib import Path

root = Path("/home/flash/nuttx-esp32p4-ref")
print("=== tools ===")
tools = root / "tools"
print(list(tools.iterdir()) if tools.is_dir() else "NO_TOOLS")
print("=== gitmodules ===")
gm = root / ".gitmodules"
print(gm.read_text() if gm.exists() else "NO_GITMODULES")
print("=== chip Kconfig ===")
print((root / "arch/risc-v/src/esp32p4/Kconfig").read_text()[:4000])
print("=== nsh defconfig ===")
print((root / "boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/nsh/defconfig").read_text())
k = root / "arch/risc-v/Kconfig"
print("=== arch Kconfig exists", k.exists(), "size", k.stat().st_size if k.exists() else 0)
if k.exists():
    text = k.read_text()
    for line in text.splitlines():
        if "esp32p4" in line.lower() or "ESP32P4" in line or "ESP32C3" in line:
            print(line)
