#!/usr/bin/env python3
"""Import pinned wireless patches from local reference Git objects, offline."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

KERNEL = (
    "arch/risc-v/src/esp32p4/Kconfig",
    "arch/risc-v/src/esp32p4/Make.defs",
    "arch/risc-v/src/esp32p4/esp32p4_sdmmc.c",
    "arch/risc-v/src/esp32p4/hardware/esp32p4_sdmmc.h",
)
SOURCES = (
    ("nuttx", "2f1387d56eb04ad2599baca58a3fa2380cdaaedb",
     "88f2644ee73207961fad3a9de14dda9c654abe13", KERNEL),
    ("apps", "88827afd368d4bbb4802b96ed44d9582f85b2f92",
     "23c90a2372409e04767a090c9753fe44c4b145bd", ("system/c6probe",)),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nuttx", type=Path, required=True)
    parser.add_argument("--apps", type=Path, required=True)
    args = parser.parse_args()
    destination = Path(__file__).resolve().parent / "patches/c6"
    if destination.exists():
        parser.error("Destination exists; refusing to overwrite pinned patches")
    manifest = {}
    files = {}
    for name, base, head, paths in SOURCES:
        repo = getattr(args, name)
        data = subprocess.check_output(
            ["git", "-C", str(repo), "diff", "--binary", base, head, "--", *paths])
        if not data:
            raise RuntimeError(f"Empty patch: {name}")
        if name == "nuttx":
            anchor = (b"@@ -25,6 +25,12 @@ include common/espressif/Make.defs\n"
                      b" \n CHIP_CSRCS += esp_chip_rev.c\n \n")
            if data.count(anchor) != 1:
                raise RuntimeError("Unexpected Make.defs patch context")
            data = data.replace(anchor, b"@@ -28,3 +28,9 @@\n", 1)
        filename = name + ".patch"
        files[filename] = data
        manifest[name] = {"repository": f"https://github.com/streetartist/esp32p4_{name if name == 'nuttx' else 'nuttx_apps'}",
                          "base": base, "head": head, "patch": filename,
                          "sha256": hashlib.sha256(data).hexdigest()}
    destination.mkdir(parents=True)
    for filename, data in files.items():
        (destination / filename).write_bytes(data)
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Imported two scoped patches into {destination}")


if __name__ == "__main__":
    main()
