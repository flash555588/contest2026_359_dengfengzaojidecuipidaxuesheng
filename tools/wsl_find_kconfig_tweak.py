#!/usr/bin/env python3
import os
import shutil
import kconfiglib
print("kconfiglib", kconfiglib.__file__)
print("PATH", os.environ.get("PATH"))
for name in ("kconfig-tweak", "menuconfig", "olddefconfig"):
    print(name, shutil.which(name))
from pathlib import Path
for p in (Path("/usr/local/bin"), Path("/usr/bin"), Path.home()/".local/bin"):
    print(p, list(p.glob("kconfig*")) if p.exists() else "missing")
