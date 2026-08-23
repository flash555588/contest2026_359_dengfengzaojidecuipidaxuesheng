#!/usr/bin/env python3
from pathlib import Path
os_c = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/src/platform/os.c")
print("realpath", os_c.resolve())
print("chip link", Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip").is_symlink(), Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/chip").resolve())
print("esp32p4 same?", Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/nuttx/src/platform/os.c").resolve() == os_c.resolve())

text = os_c.read_text(encoding="utf-8", errors="replace")
for needle in ("nxtask_", "nxsched_", "file_mq", "file_fcntl", "kthread", "task_create", "nxsem", "ATOMIC", "posix_spawn", "O_RDWR", "F_GETFL"):
    print(needle, text.count(needle))

print("==== spawn.h include guard issues? first 40 lines ====")
print("\n".join(Path("/home/flash/vela-p4/nuttx/include/spawn.h").read_text(encoding="utf-8", errors="replace").splitlines()[:40]))
