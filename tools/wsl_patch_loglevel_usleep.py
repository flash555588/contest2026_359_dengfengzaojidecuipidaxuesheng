#!/usr/bin/env python3
from pathlib import Path

os_h = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/include/platform/os.h")
text = os_h.read_text()
if "nxsched_usleep" in text:
    if "#include <nuttx/signal.h>" not in text:
        text = text.replace(
            "#include <nuttx/sched.h>",
            "#include <nuttx/sched.h>\n#include <nuttx/signal.h>",
            1,
        )
    text = text.replace("nxsched_usleep", "nxsig_usleep")
    os_h.write_text(text)
    print("patched os.h nxsched_usleep -> nxsig_usleep")
else:
    print("os.h already patched or no nxsched_usleep")

k = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/Kconfig")
kt = k.read_text()
if "config ESPRESSIF_LOG_LEVEL\n" not in kt:
    kt += """
config ESPRESSIF_LOG_LEVEL
	int
	default 1
	range 0 5
	---help---
		Espressif HAL log verbosity for sdkconfig.h (0=none .. 5=verbose).
"""
    k.write_text(kt)
    print("appended ESPRESSIF_LOG_LEVEL to Kconfig")
else:
    print("Kconfig already has ESPRESSIF_LOG_LEVEL")

cfg = Path("/home/flash/vela-p4/nuttx/.config")
ct = cfg.read_text()
if "CONFIG_ESPRESSIF_LOG_LEVEL=" not in ct:
    cfg.write_text(ct + "CONFIG_ESPRESSIF_LOG_LEVEL=1\n")
    print("added CONFIG_ESPRESSIF_LOG_LEVEL=1 to .config")
else:
    print(".config already has LOG_LEVEL")
