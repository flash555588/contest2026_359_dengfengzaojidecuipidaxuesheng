"""Exercise isolated storage writes and chat scrolling on COM23; log no chat text.

Run only after boot, with the firmware hash supplied by the current delivery.
Does not change provider settings, drafts, history, or installed applications.
The device's storage-test creates and removes its own exclusive test directory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--rounds', type=int, default=20)
parser.add_argument('--firmware-sha256', help='Actual flashed hash if a newer local build is waiting')
args = parser.parse_args()
ws = Path(__file__).resolve().parent.parent
firmware_hash = args.firmware_sha256 or hashlib.sha256((ws/'04-v3-20260913/espdl-quickapp/nuttx.bin').read_bytes()).hexdigest()
assert re.fullmatch('[0-9a-f]{64}', firmware_hash)
report = {'firmware_sha256': firmware_hash, 'rounds': []}
assert not args.output.exists()
port = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=3)
port.dtr = False
port.rts = False
port.port = 'COM23'

def command(text, limit=20):
    port.reset_input_buffer()
    started = time.monotonic()
    for part in re.findall(b'.{1,8}', (text+'\r').encode()):
        port.write(part)
        time.sleep(.03)
    data = bytearray()
    last = time.monotonic()
    while time.monotonic()-started < limit:
        chunk = port.read(max(1, port.in_waiting))
        if chunk:
            data.extend(chunk)
            last = time.monotonic()
        if b'nsh> ' in data and time.monotonic()-last > .2:
            return data.decode('utf-8', errors='replace'), round(1000*(time.monotonic()-started))
    raise RuntimeError('Device did not respond to '+text)

try:
    with port:
        command('')
        for i in range(args.rounds):
            entry = {'round': i+1}
            output, entry['storage_host_ms'] = command('desktop storage-test')
            entry['storage_pass'] = 'storage selftest PASS' in output
            assert entry['storage_pass'], 'storage test did not pass'
            direction = 'up' if i % 2 == 0 else 'down'
            output, _ = command('desktop ui chat-scroll-'+direction)
            match = re.search(r'chat-scroll render_ms=(\d+) scroll_y=(-?\d+)', output)
            if match:
                entry['scroll_render_ms'], entry['scroll_y'] = map(int, match.groups())
            output, _ = command('desktop ui chat-status')
            entry['status'] = [line for line in output.splitlines() if line.startswith(('chat loaded=', 'chat-stream '))]
            assert entry['status'], 'missing chat status'
            report['rounds'].append(entry)
            args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
            print(json.dumps(entry), flush=True)
except Exception as error:
    report['error'] = str(error)
    raise
finally:
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
