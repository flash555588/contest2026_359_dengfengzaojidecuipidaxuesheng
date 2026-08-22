#!/usr/bin/env python3
"""Copy Windows contest board/chip glue into the trees vela-p4 actually builds."""
from pathlib import Path
import shutil

WIN = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng"
)
WSL_CONTEST = Path(
    "/home/flash/openvela/contest2026_359_dengfengzaojidecuipidaxuesheng"
)
CHIP_DST = Path("/home/flash/vela-p4/nuttx/arch/risc-v/src/esp32p4")

pairs = [
    (
        WIN / "board/esp32p4-function-ev-board/src/esp32p4_appinit.c",
        WSL_CONTEST / "board/esp32p4-function-ev-board/src/esp32p4_appinit.c",
    ),
    (
        WIN / "board/esp32p4-function-ev-board/src/Make.defs",
        WSL_CONTEST / "board/esp32p4-function-ev-board/src/Make.defs",
    ),
    (
        WIN / "board/esp32p4-function-ev-board/src/CMakeLists.txt",
        WSL_CONTEST / "board/esp32p4-function-ev-board/src/CMakeLists.txt",
    ),
    (WIN / "chip/esp32p4/src/esp_atomic64.c", CHIP_DST / "esp_atomic64.c"),
    (WIN / "chip/esp32p4/src/Make.defs", CHIP_DST / "Make.defs"),
    (WIN / "chip/esp32p4/src/CMakeLists.txt", CHIP_DST / "CMakeLists.txt"),
]


def copy_text(src: Path, dst: Path) -> None:
    text = src.read_text(encoding="utf-8").replace("\r\n", "\n")
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() and dst.read_text(encoding="utf-8") == text:
        print("unchanged", dst)
        return
    dst.write_text(text, encoding="utf-8")
    print("copied", src.name, "->", dst)


def main() -> None:
    for src, dst in pairs:
        if not src.exists():
            raise SystemExit(f"missing {src}")
        copy_text(src, dst)


if __name__ == "__main__":
    main()
