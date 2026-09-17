#!/usr/bin/env python3
from pathlib import Path
import re

cfg = Path("/home/flash/vela-p4/nuttx/.config")
text = cfg.read_text(encoding="utf-8")
adds = {
    "CONFIG_ESPRESSIF_CPU_FREQ_360": "CONFIG_ESPRESSIF_CPU_FREQ_360=y\n",
    "CONFIG_ESPRESSIF_CPU_FREQ_MHZ": "CONFIG_ESPRESSIF_CPU_FREQ_MHZ=360\n",
}
for name, line in adds.items():
    if re.search(rf"^{name}=", text, re.M):
        text = re.sub(rf"^{name}=.*$", line.rstrip(), text, flags=re.M)
    elif f"# {name} is not set" in text:
        text = text.replace(f"# {name} is not set\n", line)
    else:
        text += line
    print("set", line.strip())
cfg.write_text(text, encoding="utf-8")

hdr = Path("/home/flash/vela-p4/nuttx/include/nuttx/config.h")
if hdr.is_file():
    ht = hdr.read_text(encoding="utf-8")
    if "CONFIG_ESPRESSIF_CPU_FREQ_MHZ" not in ht:
        ht = ht.replace(
            "#endif /* __INCLUDE_NUTTX_CONFIG_H */",
            "#define CONFIG_ESPRESSIF_CPU_FREQ_MHZ 360\n#endif /* __INCLUDE_NUTTX_CONFIG_H */",
        )
        if "CONFIG_ESPRESSIF_CPU_FREQ_MHZ" not in ht:
            ht += "\n#define CONFIG_ESPRESSIF_CPU_FREQ_MHZ 360\n"
        hdr.write_text(ht, encoding="utf-8")
        print("config.h patched")
    else:
        print("config.h already has CPU_FREQ_MHZ")
