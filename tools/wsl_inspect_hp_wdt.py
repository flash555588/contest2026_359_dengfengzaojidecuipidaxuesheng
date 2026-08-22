#!/usr/bin/env python3
from pathlib import Path
hal = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty")
print("==== HP WDT / hp_wdt / HP_SYS_WDT ====")
keys = ("HP_WDT", "hp_wdt", "HP_SYS_HP_WDT", "FLASHBOOT", "wdt_flashboot")
shown=set()
for p in (hal / "components/soc/esp32p4").rglob("*.h"):
    t = p.read_text(encoding="utf-8", errors="replace")
    hit = False
    for k in keys:
        if k in t:
            hit=True
            break
    if not hit:
        continue
    rel = str(p.relative_to(hal))
    if "hw_ver3" in rel:
        continue
    print("FILE", rel)
    for i,ln in enumerate(t.splitlines(),1):
        if any(k.lower() in ln.lower() for k in ("hp_wdt","hp_sys","flashboot","wdt_en")):
            if "clock" in ln.lower() and "wdt" not in ln.lower():
                continue
            print(f"  {i}|{ln[:200]}")
