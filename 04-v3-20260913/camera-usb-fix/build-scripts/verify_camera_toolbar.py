"""Check the saved board JPEG and the captured preview's reserved controls."""
from pathlib import Path
import hashlib
import json
import re
from PIL import Image

ws = Path(__file__).resolve().parent.parent
evidence = ws / '04-v3-20260913/camera-usb-fix/evidence'
cases = json.loads((evidence / 'photo-read-camera-10.json').read_text(encoding='utf-8'))
dump = next(c['output'] for c in cases if c['name'] == 'photo_bytes')
data = bytearray()
for line in dump.splitlines():
    match = re.match(r'^([0-9a-fA-F]{4}): ((?:[0-9a-fA-F]{2} ){1,16})', line)
    if match:
        assert int(match[1], 16) == len(data) % 512, (len(data), line)
        data.extend(bytes.fromhex(match[2]))
assert len(data) == 29111, len(data)
assert data[:2] == b'\xff\xd8' and data[-2:] == b'\xff\xd9'
saved = evidence / 'photo-camera-10.jpg'
saved.write_bytes(data)
with Image.open(saved) as photo:
    photo.load()
    assert photo.size == (640, 480) and photo.format == 'JPEG'
with Image.open(evidence / 'ui-toolbar-10/dark-camera-preview.png') as preview:
    bar = preview.convert('RGB').crop((0, 500, 1024, 600))
    black_pixels = sum(pixel == (0, 0, 0) for pixel in bar.getdata())
    assert black_pixels == 0, black_pixels
report = {
    'saved_jpeg_bytes': len(data), 'saved_jpeg_size': [640, 480],
    'saved_jpeg_sha256': hashlib.sha256(data).hexdigest(),
    'independent_jpeg_decode': 'passed',
    'preview_toolbar_black_pixels': black_pixels,
    'scope': 'One captured frame; temporal flicker still needs observation on the physical screen.'
}
(evidence / 'toolbar-image-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
