#!/usr/bin/env python3
from pathlib import Path

spawn = Path("/home/flash/vela-p4/nuttx/include/spawn.h").read_text(encoding="utf-8", errors="replace")
i = spawn.find("struct posix_spawnattr_s")
print(spawn[i:i+1200])

print("\n==== posix_spawnattr_init ====")
for f in Path("/home/flash/vela-p4/nuttx/libs/libc/spawn").glob("*.c"):
    t = f.read_text(encoding="utf-8", errors="replace")
    if "posix_spawnattr_init" in t and "int posix_spawnattr_init" in t:
        print(f)
        print(t[t.find("int posix_spawnattr_init"):t.find("int posix_spawnattr_init")+600])

print("\n==== contest chip overlay ====")
for p in Path("/mnt/c/Users/flash/Desktop/openvela-contest/contest2026_359_dengfengzaojidecuipidaxuesheng/chip/esp32p4").rglob("*"):
    if p.is_file() and ("os.c" in p.name or "patch" in p.name.lower() or "hal" in str(p).lower()):
        print(p)

print("\n==== vendor patches s3? ====")
for base in [
    Path("/home/flash/openvela/vendor"),
    Path("/home/flash/vela-p4"),
]:
    if not base.exists():
        print("missing", base)
        continue
    for p in base.rglob("*os.c*"):
        if "hal" in str(p).lower() or "espressif" in str(p).lower():
            print(p)
