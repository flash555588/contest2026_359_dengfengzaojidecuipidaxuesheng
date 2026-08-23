#!/usr/bin/env python3
from pathlib import Path
p = Path("/home/flash/vela-p4/nuttx/drivers/drivers_initialize.c")
text = p.read_text()
old = "#include <nuttx/usrsock/usrsock_rpmsg.h>\n"
new = "#ifdef CONFIG_NET\n#  include <nuttx/usrsock/usrsock_rpmsg.h>\n#endif\n"
if old not in text:
    if "#ifdef CONFIG_NET" in text and "usrsock_rpmsg.h" in text:
        print("already patched")
    else:
        raise SystemExit("needle not found")
else:
    p.write_text(text.replace(old, new, 1))
    print("patched drivers_initialize.c")
