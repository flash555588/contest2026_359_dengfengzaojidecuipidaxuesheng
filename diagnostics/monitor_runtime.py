"""Observe uptime and heaps on one serial connection, without JTAG or resets."""
from pathlib import Path
import json
import re
import serial
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
root = Path(__file__).resolve().parent
port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
port.dtr = False
port.rts = False
port.port = 'COM23'
samples = []
captured = bytearray()

with port, (root / 'runtime-monitor.log').open('xb') as log:
    def read_until_prompt(command):
        port.write(command.encode('ascii') + b'\r')
        response = bytearray()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            data = port.read(max(1, port.in_waiting))
            response.extend(data)
            captured.extend(data)
            log.write(data)
            log.flush()
            if b'nsh>' in response and response.endswith(b'\x1b[K'):
                return response.decode('utf-8', errors='replace')
        raise RuntimeError(f'No NSH response for {command!r}')

    read_until_prompt('')
    started = time.monotonic()
    for index in range(4):
        target = started + index * 30
        while time.monotonic() < target:
            data = port.read(max(1, port.in_waiting))
            captured.extend(data)
            log.write(data)
            log.flush()
        uptime_text = read_until_prompt('cat /proc/uptime')
        uptime = float(re.search(r'^\s*(\d+\.\d+)\s*$', uptime_text, re.M)[1])
        heaps_text = read_until_prompt('free')
        heaps = {}
        for line in heaps_text.splitlines():
            fields = line.split()
            if fields and fields[-1] in ('Umem', 'Kmem'):
                heaps[fields[-1]] = dict(zip(['total', 'used', 'free', 'maxused', 'maxfree', 'nused', 'nfree'], map(int, fields[:-1])))
        sample = {'elapsed_seconds': time.monotonic() - started,
                  'uptime_seconds': uptime, 'heaps': heaps}
        samples.append(sample)
        print(json.dumps(sample), flush=True)
    tasks = read_until_prompt('ps')
    print(tasks, flush=True)

report = {'samples': samples, 'reset_banner_observed': b'ESP-ROM:' in captured,
          'uptime_increased': all(b['uptime_seconds'] > a['uptime_seconds'] for a, b in zip(samples, samples[1:])),
          'kernel_used_delta': samples[-1]['heaps']['Kmem']['used'] - samples[0]['heaps']['Kmem']['used'],
          'desktop_alive': bool(re.search(r'\bdesktop\s*$', tasks, re.M)),
          'timer_alive': 'esp_timer' in tasks}
(root / 'runtime-monitor.json').write_text(json.dumps(report, indent=2) + '\n')
assert report['uptime_increased'] and not report['reset_banner_observed']
assert report['desktop_alive'] and report['timer_alive']
assert report['kernel_used_delta'] == 0
print('Continuous serial observation passed', flush=True)
