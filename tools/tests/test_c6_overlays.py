"""Offline checks for the optional C6 overlay delivery."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
import subprocess
import tempfile
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("c6_overlay", TOOLS / "apply_c6_overlays.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class OverlayTest(unittest.TestCase):
    def test_make_patch_preserves_atomic_source(self):
        text = (TOOLS / "patches/c6/nuttx.patch").read_text()
        marker = "diff --git a/arch/risc-v/src/esp32p4/Make.defs "
        section = marker + text.split(marker, 1)[1].split("diff --git ", 1)[0]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "arch/risc-v/src/esp32p4/Make.defs"
            target.parent.mkdir(parents=True)
            original = ("include common/espressif/Make.defs\n\n"
                        "CHIP_CSRCS += esp_chip_rev.c esp_atomic64.c\n\n"
                        "ifeq ($(CONFIG_SMP),y)\nCHIP_CSRCS += esp32p4_smp.c\nendif\n")
            target.write_text(original, newline="\n")
            patchfile = root / "input.patch"
            patchfile.write_text(section, newline="\n")
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "apply", str(patchfile)], check=True)
            result = target.read_text()
            self.assertIn("esp_chip_rev.c esp_atomic64.c", result)
            self.assertEqual(result.count("CHIP_CSRCS += esp32p4_sdmmc.c"), 1)
            self.assertEqual(module.check(root, patchfile, reverse=True).returncode, 0)

    def test_hashes_and_scope(self):
        root = TOOLS / "patches/c6"
        manifest = json.loads((root / "manifest.json").read_text())
        for name, entry in manifest.items():
            data = (root / entry["patch"]).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])
            paths = [line.split()[3][2:] for line in data.decode().splitlines()
                     if line.startswith("diff --git ")]
            self.assertTrue(paths)
            if name == "apps":
                self.assertTrue(all(p.startswith("system/c6probe/") for p in paths))
            else:
                self.assertEqual(set(paths), {
                    "arch/risc-v/src/esp32p4/Kconfig",
                    "arch/risc-v/src/esp32p4/Make.defs",
                    "arch/risc-v/src/esp32p4/esp32p4_sdmmc.c",
                    "arch/risc-v/src/esp32p4/hardware/esp32p4_sdmmc.h"})

    def test_check_never_writes(self):
        with patch.object(module.subprocess, "run") as run:
            module.check(Path("tree"), Path("patch"))
            self.assertIn("--check", run.call_args.args[0])
            module.check(Path("tree"), Path("patch"), reverse=True)
            self.assertIn("--check", run.call_args.args[0])
            self.assertIn("--reverse", run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
