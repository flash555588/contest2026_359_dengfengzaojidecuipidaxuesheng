"""Check the packaged development artifacts against the written manifest."""
from pathlib import Path
import hashlib

delivery = Path(__file__).resolve().parent.parent / '04-v3-20260913/camera-usb-fix'
rows = [line.split('  ', 1) for line in (delivery / 'SHA256SUMS').read_text().splitlines()]
for expected, name in rows:
    assert hashlib.sha256((delivery / name).read_bytes()).hexdigest() == expected, name
print('PASS:', len(rows), 'packaged file hashes verified')
