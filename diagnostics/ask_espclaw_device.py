"""Run one explicit ESPClaw CLI request and record its real tool-call flow.

Uses the device's existing provider configuration, without reading credentials
or modifying chat history. The question must fit the board's NSH input limit.
"""
from pathlib import Path
import argparse
import json
import re
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('question')
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--timeout', type=float, default=260)
args = parser.parse_args()
assert '"' not in args.question and '\n' not in args.question and '\r' not in args.question
command = 'espclaw ask "' + args.question + '"'
assert len(command.encode('utf-8')) <= 78, 'Question exceeds NSH command length'
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open('x', encoding='utf-8') as initial:
    initial.write('{}\n')
port = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=3)
port.dtr = False; port.rts = False; port.port = 'COM23'
raw = bytearray()
started = time.monotonic()
completed = False
last_update = 0
last_save = 0


def report():
    text = raw.decode('utf-8', errors='replace').replace('\r', '')
    clean = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', text)
    return dict(command=command, seconds=round(time.monotonic() - started, 2),
                completed=completed, command_failed='espclaw: command failed' in clean,
                tools=re.findall(r'tool_call[^\n]*?name=(qpk_[a-z_]+)', clean), output=text)


try:
    with port:
        port.reset_input_buffer()
        encoded = command.encode('utf-8') + b'\r'
        for index in range(0, len(encoded), 8):
            port.write(encoded[index:index + 8]); time.sleep(.03)
        while time.monotonic() - started < args.timeout:
            raw.extend(port.read(max(1, port.in_waiting)))
            text = raw.decode('utf-8', errors='replace')
            if re.search(r'(?:^|[\r\n])nsh> ', text):
                completed = True
                break
            now = time.monotonic()
            if now - last_save > 1:
                args.output.write_text(json.dumps(report(), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                last_save = now
            if now - last_update > 20:
                status = report()
                print(json.dumps({key: status[key] for key in ('seconds', 'tools')}, ensure_ascii=False), flush=True)
                last_update = now
finally:
    result = report()
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: result[key] for key in ('completed', 'command_failed', 'seconds', 'tools')}, ensure_ascii=False), flush=True)
if not completed or result['command_failed']:
    raise SystemExit(1)
