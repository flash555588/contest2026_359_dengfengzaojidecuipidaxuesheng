"""Run a bounded, reviewed suite of NSH commands and preserve responses."""
import argparse
import json
from pathlib import Path
import re
import serial
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('suite', type=Path)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--start', type=int, default=0)
args = parser.parse_args()
suite = json.loads(args.suite.read_text(encoding='utf-8'))
results = []
port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
port.dtr = False
port.rts = False
port.port = 'COM23'
with port, args.output.with_suffix('.log').open('xb') as log:
    for case in [{'name': 'console', 'command': ''}, *suite[args.start:]]:
        command = case['command']
        if len(command.encode('utf-8')) > 78 or '\n' in command or '\r' in command:
            raise ValueError('Command exceeds NSH line limit or contains newline')
        log.write(f'\n[CASE {case["name"]}]\n'.encode())
        started = time.monotonic()
        # The target has a 64-byte RX ring and echoes through USB packets.
        # Pace commands so echo/back-pressure cannot drop an input byte.
        data = bytearray()
        encoded = command.encode() + b'\r'
        for offset in range(0, len(encoded), 8):
            port.write(encoded[offset:offset + 8])
            time.sleep(0.03)
            if port.in_waiting:
                chunk = port.read(port.in_waiting)
                data.extend(chunk)
                log.write(chunk)
        finished = False
        deadline = started + case.get('timeout', 10)
        while time.monotonic() < deadline:
            chunk = port.read(max(1, port.in_waiting))
            data.extend(chunk)
            log.write(chunk)
            log.flush()
            prompt = case.get('prompt', 'nsh> ').encode()
            if prompt in data and (data.endswith(b'\x1b[K') or data.endswith(prompt)):
                finished = True
                break
        text = data.decode('utf-8', errors='replace').replace('\r', '')
        checks = {pattern: bool(re.search(pattern, text, re.M)) for pattern in case.get('expect', [])}
        result = {'name': case['name'], 'command': command, 'completed': finished,
                  'elapsed_seconds': time.monotonic() - started, 'checks': checks,
                  'output': text}
        results.append(result)
        args.output.write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(f'[{case["name"]}] complete={finished} checks={checks}\n{text}', flush=True)
        if not finished:
            raise SystemExit('NSH command did not return; stopped before further commands')
