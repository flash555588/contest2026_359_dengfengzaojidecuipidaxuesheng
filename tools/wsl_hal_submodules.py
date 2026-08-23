#!/usr/bin/env python3
import os
import subprocess
from pathlib import Path

HAL = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty")
print((HAL / ".gitmodules").read_text()[:2000])
print("--- mbedtls ---")
print((HAL / "components/mbedtls/mbedtls").exists(), list((HAL / "components/mbedtls").iterdir())[:10])
