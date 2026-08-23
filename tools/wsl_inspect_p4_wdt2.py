#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")

# CLIC / ROM cache flags
cfg = Path("/home/flash/vela-p4/nuttx/include/nuttx/config.h").read_text(encoding="utf-8", errors="replace")
for s in ("SOC_INT_CLIC", "ESP_ROM_NEEDS", "ESPRESSIF_REGION", "RISCV_PERCPU", "DEBUG_FEATURES"):
    hits = [ln for ln in cfg.splitlines() if s in ln]
    print("CFG", s, hits[:6])

sdk = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/esp32p4/include/sdkconfig.h")
t = sdk.read_text(encoding="utf-8", errors="replace")
for s in ("CLIC", "FLASHBOOT", "WDT", "APP_BUILD_TYPE", "CACHE_MMU"):
    print("--- sdk", s)
    for ln in t.splitlines():
        if s in ln:
            print(ln)

print("==== flashboot / hp wdt ====")
for p in hal.rglob("*.h"):
    try:
        tt = p.read_text(encoding="utf-8", errors="replace")
    except Exception:
        continue
    if "flashboot" in tt.lower() or "HP_WDT" in tt or "hp_wdt" in tt:
        if "flashboot" in tt.lower() or "HP_SYS" in tt:
            print(p.relative_to(hal))

print("==== bootloader_esp32p4 current skip ====")
bl = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/components/bootloader_support/src/esp32p4/bootloader_esp32p4.c")
text = bl.read_text(encoding="utf-8", errors="replace")
i = text.find("bootloader_init(void)")
print(text[i:i+2500])
