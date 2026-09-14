"""Produce independent JPEG samples and the standard non-optimized DHT."""
from pathlib import Path
from io import BytesIO
from PIL import Image
ws = Path(__file__).resolve().parent.parent
out = ws / 'diagnostics/camera-tests'
out.mkdir(exist_ok=True)
im = Image.new('RGB', (640, 480), (200, 80, 35))
buffer = BytesIO()
im.save(buffer, 'JPEG', quality=75, optimize=False)
data = buffer.getvalue()
dht = b''
without = bytearray(data[:2])
p = 2
while data[p:p+2] != b'\xff\xda':
    n = int.from_bytes(data[p+2:p+4], 'big') + 2
    segment = data[p:p+n]
    if data[p+1] == 0xc4: dht += segment
    else: without += segment
    p += n
without += data[p:]
assert len(dht) <= 512
(out / 'sample.jpg').write_bytes(data)
(out / 'sample-no-dht.jpg').write_bytes(without)
header = '/* JPEG Annex K default Huffman tables; generated with optimize=False. */\n'
header += 'static const unsigned char g_mjpeg_dht[] = {\n'
for i in range(0, len(dht), 16): header += '  ' + ', '.join(f'0x{x:02x}' for x in dht[i:i+16]) + ',\n'
header += '};\n'
(ws / '04-v3-20260913/camera-usb-fix/overlay/apps/system/desktop/qpk_mjpeg_dht.h').write_text(header)
print('MJPEG fixtures and default DHT:', len(dht))
