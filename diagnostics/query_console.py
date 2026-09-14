"""Collect read-only NSH status commands from the repaired board."""
from pathlib import Path
import argparse
import time
import serial
import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=root / 'console-status.log')
parser.add_argument('--port', default='COM23')
parser.add_argument('--reset-reason', action='store_true',
                    help='Read ESP32-P4 LP_CLKRST_RESET_CAUSE (0x50111010)')
args = parser.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
port.dtr = False
port.rts = False
port.port = args.port
with port, args.output.open('xb') as log:
    commands = ['\r', 'uname -a\r', 'cat /proc/uptime\r', 'free\r', 'ps\r', 'ls /dev\r']
    if args.reset_reason:
        commands.append('xd 0x50111010 4\r')
    for command in commands:
        port.write(command.encode())
        end = time.monotonic() + 6
        received = bytearray()
        while time.monotonic() < end:
            data = port.read(max(1, port.in_waiting))
            received.extend(data)
            if data:
                log.write(data)
                log.flush()
            if b'nsh>' in received and received.endswith(b'\x1b[K'):
                break
        print(received.decode('utf-8', errors='replace'), end='', flush=True)
