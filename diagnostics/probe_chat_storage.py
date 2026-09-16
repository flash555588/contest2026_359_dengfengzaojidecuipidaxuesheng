"""Back up the device's chat files over NSH; print only sizes and validation results."""
from pathlib import Path
import hashlib
import json
import re
import time
import serial

ws = Path(__file__).resolve().parent.parent
out = ws / 'diagnostics/private/chat-storage-71'
out.mkdir(parents=True, exist_ok=True)
port = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=3)
port.dtr = False
port.rts = False
port.port = 'COM23'


def command(text):
    data = bytearray()
    encoded = (text + '\r').encode()
    for offset in range(0, len(encoded), 8):
        port.write(encoded[offset:offset+8])
        time.sleep(.03)
    until = time.monotonic() + 10
    quiet = time.monotonic()
    while time.monotonic() < until:
        chunk = port.read(max(1, port.in_waiting))
        if chunk:
            data.extend(chunk)
            quiet = time.monotonic()
        if b'nsh> ' in data and time.monotonic() - quiet > .3:
            return bytes(data)
    raise RuntimeError('NSH backup read timed out')


report = []
with port:
    command('')
    for name, size in [('history.json', 0), ('history.tmp', 160)]:
        raw = command('hexdump /data/config/chat/' + name)
        (out / (name + '.serial')).write_bytes(raw)
        chunks = []
        for line in raw.decode('ascii', errors='replace').splitlines():
            match = re.match(r'^\s*(?:0x)?[0-9a-fA-F]{4,16}[: ]\s*((?:[0-9a-fA-F]{2}(?:\s+|$)){1,16})', line)
            if match:
                chunks.append(bytes.fromhex(match.group(1)))
        content = b''.join(chunks)
        item = {'name': name, 'expected_bytes': size, 'exported_bytes': len(content), 'complete': len(content) == size}
        if len(content) == size:
            (out / name).write_bytes(content)
            item['sha256'] = hashlib.sha256(content).hexdigest()
            try:
                doc = json.loads(content)
                item.update({'valid_json': True, 'version': doc.get('version'), 'turns': len(doc.get('turns', [])),
                             'states': [t.get('state') for t in doc.get('turns', [])]})
            except (ValueError, TypeError):
                item['valid_json'] = False
        else:
            item['format_shapes'] = [re.sub(r'\w', 'x', line)[:85] for line in raw.decode(errors='replace').splitlines()[:5]]
        report.append(item)
print(json.dumps(report, ensure_ascii=True, indent=2))
(ws / '04-v3-20260913/espdl-quickapp/evidence/chat-storage-backup-71.json').write_text(
    json.dumps(report, indent=2) + '\n', encoding='utf-8')
