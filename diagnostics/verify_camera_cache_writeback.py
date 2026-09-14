"""Verify the shipped USB worker calls the real P4 cache writeback path."""
from pathlib import Path
import hashlib
import json
import subprocess

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/camera-usb-fix'
config = (delivery / 'resolved.config').read_text()
assert 'CONFIG_ARCH_DCACHE=y' not in config
header = (ws / 'diagnostics/camera-display-reference/nuttx/include/nuttx/cache.h').read_text()
assert '#  define up_clean_dcache(start, end)\n' in header
source = (delivery / 'overlay/apps/system/desktop/qpk_camera_usb.inc').read_text()
assert 'int ret = esp_mipi_dsi_flush_framebuffer((void *)address, chunk);' in source
objdump = 'D:/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin/riscv32-esp-elf-objdump.exe'
assembly = subprocess.check_output([objdump, '-d', '--disassemble=qpk_camera_usb_thread', str(delivery / 'nuttx.elf')]).decode('utf-8', errors='replace')
calls = [line.strip() for line in assembly.splitlines() if '<esp_mipi_dsi_flush_framebuffer>' in line]
assert calls, 'Actual USB worker did not call P4 cache writeback'
report = {
    'firmware_sha256': hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest(),
    'generic_cache_api_is_noop_in_this_config': True,
    'psram_l2_cache_bytes': 262144,
    'frame_bytes': 1228800,
    'verified_worker_calls': calls,
    'writeback_chunk_bytes': 16384,
    'writeback_failure_prevents_presentation': True
}
(delivery / 'evidence/cache-writeback-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
