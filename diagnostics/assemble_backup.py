"""Complete the original application backup from overlapping USB/JTAG reads."""
from pathlib import Path
import hashlib
import json

root = Path(__file__).resolve().parent
prefix = (root / 'current-app-region.bin.partial').read_bytes()
tail = (root / 'original-irom-tail.bin').read_bytes()
drom = (root / 'original-drom.bin').read_bytes()
assert len(tail) == 0x10000 and len(drom) == 0x1142f0
overlap = len(prefix) - (0x2a0000 - 0x2000)
assert overlap > 0 and prefix[-overlap:] == tail[:overlap], 'USB/JTAG overlap mismatch'
image = prefix[:0x2a0000 - 0x2000] + tail + drom
metadata = json.loads((root.parent / '04-v3-20260913/20260914-BUILD-METADATA.json').read_text())
digest = hashlib.sha256(image).hexdigest()
assert len(image) == metadata['bytes']
assert digest == metadata['sha256']['nuttx.bin'], 'Original firmware hash mismatch'
destination = root / 'original-nuttx.bin'
with destination.open('xb') as output:
    output.write(image)
print(f'Original application fully backed up: {len(image)} bytes, SHA256 {digest}')
