"""Flash the reviewed application image, leaving /data at 0x400000 intact."""
from pathlib import Path
import hashlib
import subprocess
import sys

workspace = Path(__file__).resolve().parent.parent
backup = workspace / 'diagnostics/original-nuttx.bin'
assert hashlib.sha256(backup.read_bytes()).hexdigest() == '77e36cc5e6d85216af739cbe0ae2feca06e9d937e4cbd5fe808cef1a18e3521f'
firmware = workspace / '04-v3-20260913/black-screen-fix/nuttx.bin'
data = firmware.read_bytes()
assert 0x2000 + ((len(data) + 4095) & ~4095) <= 0x400000
assert data[0] == 0xe9 and data[12:14] == b'\x12\x00'
assert hashlib.sha256(data).hexdigest() == '61d7d1c8f03a1c01d50b1dfe8c9df9415933a01f7a8e6c752587d76b0659a6fe'
command = [sys.executable, '-m', 'esptool', '--chip', 'esp32p4', '--port', 'COM23',
           '--baud', '921600', '--before', 'usb-reset', '--after', 'no-reset',
           'write-flash', '0x2000', str(firmware)]
with (workspace / 'diagnostics/flash-fixed-v2.log').open('x', encoding='utf-8') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
print((workspace / 'diagnostics/flash-fixed-v2.log').read_text(encoding='utf-8', errors='replace')[-1000:])
raise SystemExit(result.returncode)
