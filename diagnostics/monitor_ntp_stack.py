"""Sample NTP stack high-water usage and UI responsiveness without JTAG."""
from pathlib import Path
import argparse
import json
import re
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=float, default=360)
parser.add_argument('--interval', type=float, default=30)
args = parser.parse_args()
records = []
start = time.monotonic()


def save():
    args.output.write_text(json.dumps(records, ensure_ascii=False, indent=2) + '\n',
                           encoding='utf-8')


with serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=3) as port:
    port.dtr = False
    port.rts = False
    port.port = 'COM23'
    port.open()

    def capture(command, seconds=2):
        entry = {'elapsed': round(time.monotonic() - start, 2),
                 'command': command, 'output': ''}
        records.append(entry)
        if command:
            encoded = (command + '\r').encode()
            for offset in range(0, len(encoded), 8):
                port.write(encoded[offset:offset + 8])
                time.sleep(.03)
        end = time.monotonic() + seconds
        raw = bytearray()
        try:
            while time.monotonic() < end:
                raw.extend(port.read(max(1, port.in_waiting)))
        finally:
            entry['output'] = raw.decode('utf-8', errors='replace').replace('\r', '')
            save()
        return entry['output']

    deadline = start + args.seconds
    while time.monotonic() < deadline:
        iteration = time.monotonic()
        tasks = capture('ps')
        task = re.search(r'^\s*(\d+)\s+.*\bNTP_daemon\b', tasks, re.M)
        if not task or 'nsh>' not in tasks:
            raise RuntimeError('NTP task or shell response missing; inspect evidence')
        stack = capture('cat /proc/%s/stack' % task[1])
        used = re.search(r'StackUsed:\s*(\d+)', stack)
        size = re.search(r'StackSize:\s*(\d+)', stack)
        if not used or not size:
            raise RuntimeError('Stack statistics missing')
        uptime = capture('uptime')
        audit = capture('desktop ui audit')
        if 'UI_LAYOUT_END' not in audit or 'nsh>' not in audit:
            raise RuntimeError('Desktop did not answer')
        print('t=%ds NTP stack=%s/%s bytes; shell and desktop responsive' %
              (time.monotonic() - start, used[1], size[1]), flush=True)
        remaining = min(deadline, iteration + args.interval) - time.monotonic()
        if remaining > 0:
            capture('', remaining)

print('Completed %.1f seconds' % (time.monotonic() - start), flush=True)
