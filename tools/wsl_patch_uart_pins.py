#!/usr/bin/env python3
from pathlib import Path

k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Kconfig")
text = k.read_text()
old = """config ESPRESSIF_UART0_TXPIN
	int "UART0 TX Pin"
	default 21 if ESPRESSIF_ESP32C3
	default 16 if ESPRESSIF_ESP32C6
	default 24 if ESPRESSIF_ESP32H2
	range 0 21 if ESPRESSIF_ESP32C3
	range 0 30 if ESPRESSIF_ESP32C6
	range 0 27 if ESPRESSIF_ESP32H2
"""
new = """config ESPRESSIF_UART0_TXPIN
	int "UART0 TX Pin"
	default 37 if ARCH_CHIP_ESP32P4 || ESPRESSIF_ESP32P4
	default 21 if ESPRESSIF_ESP32C3
	default 16 if ESPRESSIF_ESP32C6
	default 24 if ESPRESSIF_ESP32H2
	range 0 54 if ARCH_CHIP_ESP32P4 || ESPRESSIF_ESP32P4
	range 0 21 if ESPRESSIF_ESP32C3
	range 0 30 if ESPRESSIF_ESP32C6
	range 0 27 if ESPRESSIF_ESP32H2
"""
if "default 37 if ARCH_CHIP_ESP32P4" not in text:
    if old not in text:
        raise SystemExit("TXPIN block not found")
    text = text.replace(old, new, 1)
oldr = """config ESPRESSIF_UART0_RXPIN
	int "UART0 RX Pin"
	default 20 if ESPRESSIF_ESP32C3
	default 17 if ESPRESSIF_ESP32C6
	default 23 if ESPRESSIF_ESP32H2
	range 0 21 if ESPRESSIF_ESP32C3
	range 0 30 if ESPRESSIF_ESP32C6
	range 0 27 if ESPRESSIF_ESP32H2
"""
newr = """config ESPRESSIF_UART0_RXPIN
	int "UART0 RX Pin"
	default 38 if ARCH_CHIP_ESP32P4 || ESPRESSIF_ESP32P4
	default 20 if ESPRESSIF_ESP32C3
	default 17 if ESPRESSIF_ESP32C6
	default 23 if ESPRESSIF_ESP32H2
	range 0 54 if ARCH_CHIP_ESP32P4 || ESPRESSIF_ESP32P4
	range 0 21 if ESPRESSIF_ESP32C3
	range 0 30 if ESPRESSIF_ESP32C6
	range 0 27 if ESPRESSIF_ESP32H2
"""
if "default 38 if ARCH_CHIP_ESP32P4" not in text:
    if oldr not in text:
        raise SystemExit("RXPIN block not found")
    text = text.replace(oldr, newr, 1)
k.write_text(text)
print("kconfig uart pins patched")

cfg = Path("/home/flash/vela-p4/nuttx/.config")
ct = cfg.read_text()
adds = {
    "CONFIG_ESPRESSIF_UART0=y": "CONFIG_ESPRESSIF_UART0=y\n",
    "CONFIG_ESPRESSIF_UART0_TXPIN=37": "CONFIG_ESPRESSIF_UART0_TXPIN=37\n",
    "CONFIG_ESPRESSIF_UART0_RXPIN=38": "CONFIG_ESPRESSIF_UART0_RXPIN=38\n",
    "CONFIG_UART0_BAUD=115200": "CONFIG_UART0_BAUD=115200\n",
}
for key, line in adds.items():
    name = key.split("=")[0]
    if f"{name}=" not in ct:
        ct += line
        print("added", key)
    else:
        # force pins
        if name.endswith("TXPIN") or name.endswith("RXPIN") or name == "CONFIG_ESPRESSIF_UART0":
            import re
            ct = re.sub(rf"^{name}=.*$", key, ct, flags=re.M)
            ct = ct.replace(f"# {name} is not set\n", line)
            print("set", key)
cfg.write_text(ct)
