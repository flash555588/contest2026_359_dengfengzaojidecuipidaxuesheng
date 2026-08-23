#!/usr/bin/env python3
from pathlib import Path
mb = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/mbedtls/mbedtls")
print("is_dir", mb.is_dir(), "is_git", (mb/".git").exists())
entries = list(mb.iterdir()) if mb.exists() else []
print("count", len(entries))
print([p.name for p in entries[:20]])
psa = list(mb.rglob("crypto.h"))
print("crypto.h", psa[:5])
# include path used by compile: mbedtls/include
print("include", (mb/"include").exists(), (mb/"tf-psa-crypto").exists())
