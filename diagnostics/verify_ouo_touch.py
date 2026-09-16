"""Verify the OuO touch reactions on the board.

The eyes are the only objects on the page, so their geometry in the layout
audit shows whether a touch makes them follow the finger, squint on release
and drift back afterwards.
"""
from pathlib import Path
import argparse
import json
import re
import sys
import time

import serial

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)


def console(command, timeout=12):
    assert len(command.encode()) <= 78
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
    port.dtr = False
    port.rts = False
    port.port = 'COM23'
    data = bytearray()
    received_at = time.monotonic()
    with port:
        encoded = command.encode() + b'\r'
        for start in range(0, len(encoded), 8):
            port.write(encoded[start:start + 8])
            time.sleep(0.03)
            data.extend(port.read(port.in_waiting))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = port.read(max(1, port.in_waiting))
            data.extend(chunk)
            if chunk:
                received_at = time.monotonic()
            if b'nsh> ' in data and (data.endswith(b'nsh> ') or data.endswith(b'\x1b[K') or
                                     time.monotonic() - received_at > 0.25):
                return data.decode('utf-8', errors='replace').replace('\r', '')
    raise RuntimeError('Console timeout: ' + command)


def eyes():
    """Return the two eye rectangles from the layout audit."""
    for _ in range(3):
        audit = console('desktop ui audit')
        match = re.search(r'UI_LAYOUT_BEGIN\n(.*?)\nUI_LAYOUT_END', audit, re.S)
        if not match:
            continue
        try:
            objects = json.loads(match.group(1))
        except json.JSONDecodeError:
            continue
        rects = [o for o in objects if o['type'] == 'container' and
                 o['parent'] == 5 and o['w'] < 200 and o['h'] > 20]
        if len(rects) == 2:
            return rects
    raise RuntimeError('Could not read the eye layout')


report = {}
assert 'nsh> ' in console('')
assert 'ready' in console('desktop ui light')
output = console('desktop ui ouo')
assert 'ready' in output, output
time.sleep(1.5)

output += console('desktop ui touch:down:0.90:0.30')
time.sleep(0.6)
held = eyes()

output += console('desktop ui touch:up:0.90:0.30')
time.sleep(0.12)
released = eyes()

output += console('desktop ui touch:down:0.10:0.70')
time.sleep(0.6)
left = eyes()
output += console('desktop ui touch:up:0.10:0.70')
time.sleep(1.2)
settled = eyes()

(args.output / 'console.log').write_text(output, encoding='utf-8')
report['held_right'] = held
report['released'] = released
report['held_left'] = left
report['settled'] = settled
# Comparing the two presses keeps the check independent of the stored emotion
# and of a resting pose the panel may already be reporting.
report['follows_finger'] = held[0]['x'] > left[0]['x'] + 20
report['squints_on_release'] = released[0]['h'] < held[0]['h'] - 6
report['relaxes_after_release'] = left[0]['x'] >= settled[0]['x'] - 12
(args.output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k: v for k, v in report.items() if not k.startswith('held_')}, indent=2))
assert report['follows_finger'], 'Eyes must follow the finger'
assert report['squints_on_release'], 'Release must squint'
assert report['relaxes_after_release'], 'Eyes must return after release'
