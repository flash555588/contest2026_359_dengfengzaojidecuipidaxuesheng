"""Flash the approved Wi-Fi image to the identified board and save evidence."""
from pathlib import Path
import hashlib
import subprocess
import sys
import serial.tools.list_ports

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/wifi-dhcp-fix'
firmware = delivery / 'nuttx.bin'
data = firmware.read_bytes()
assert hashlib.sha256(data).hexdigest() == '559e8f13dc4f1627048c6a5250383762d96262f3b892018382a1a68c60bca953'
assert data[0] == 0xe9 and data[12:14] == b'\x12\x00'
assert 0x2000 + ((len(data) + 4095) & ~4095) <= 0x400000
ports = [p for p in serial.tools.list_ports.comports() if p.device == 'COM23']
assert len(ports) == 1 and ports[0].serial_number == 'E8:F6:0A:E3:A9:5F'
assert (ports[0].vid, ports[0].pid) == (0x303a, 0x1001)
rollback = workspace / '04-v3-20260913/black-screen-fix/nuttx.bin'
assert hashlib.sha256(rollback.read_bytes()).hexdigest() == '61d7d1c8f03a1c01d50b1dfe8c9df9415933a01f7a8e6c752587d76b0659a6fe'
command = [sys.executable, '-m', 'esptool', '--chip', 'esp32p4', '--port', 'COM23',
           '--baud', '921600', '--before', 'usb-reset', '--after', 'no-reset',
           'write-flash', '0x2000', str(firmware)]
path = delivery / 'evidence/flash.log'
with path.open('x', encoding='utf-8') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
print(path.read_text(encoding='utf-8', errors='replace')[-2500:])
raise SystemExit(result.returncode)
