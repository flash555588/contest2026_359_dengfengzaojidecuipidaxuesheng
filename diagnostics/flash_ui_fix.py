"""Flash the validated storage fix to the identified board, preserving /data."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import serial.tools.list_ports

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/ui-layout-fix'
firmware = delivery / 'nuttx.bin'
data = firmware.read_bytes()
expected = '038b09a89f4a33e14fce5b8227cf373c63d2fb58705d363a4169464ce1765090'
assert hashlib.sha256(data).hexdigest() == expected
validation = json.loads((delivery / 'evidence/validation.json').read_text())
assert validation['firmware_sha256'] == expected and validation['status'] == 'static-checks-passed'
assert data[0] == 0xe9 and data[12:14] == b'\x12\x00'
assert 0x2000 + ((len(data) + 4095) & ~4095) <= 0x400000
ports = [p for p in serial.tools.list_ports.comports() if p.device == 'COM23']
assert len(ports) == 1 and ports[0].serial_number == 'E8:F6:0A:E3:A9:5F'
assert (ports[0].vid, ports[0].pid) == (0x303a, 0x1001)
rollback = workspace / '04-v3-20260913/app-storage-fix/nuttx.bin'
assert hashlib.sha256(rollback.read_bytes()).hexdigest() == '2159a0adca9a7e76680233064d907aa3676b6d10da12b1e35dae3901f7b65932'
command = [sys.executable, '-m', 'esptool', '--chip', 'esp32p4', '--port', 'COM23',
           '--baud', '921600', '--before', 'usb-reset', '--after', 'no-reset',
           'write-flash', '0x2000', str(firmware)]
path = delivery / 'evidence/flash.log'
with path.open('x', encoding='utf-8') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
print(path.read_text(encoding='utf-8', errors='replace')[-2500:])
raise SystemExit(result.returncode)
