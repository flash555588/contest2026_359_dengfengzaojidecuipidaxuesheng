"""Save primary references for C6 capability and classic A2DP source APIs."""
from pathlib import Path
import hashlib
import json
import urllib.request

root = Path(__file__).resolve().parent.parent
folder = root / '04-v3-20260913/ble-device-names/evidence/audio-reference'
folder.mkdir(exist_ok=True)
references = {}
for name, url in {
    'c6-bluetooth': 'https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/bluetooth/index.html',
    'esp32-a2dp': 'https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_a2dp.html',
}.items():
    request = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read()
        resolved = response.url
    (folder / (name + '.html')).write_bytes(data)
    references[name] = {'url': url, 'resolved_url': resolved, 'sha256': hashlib.sha256(data).hexdigest()}
(folder / 'sources.json').write_text(json.dumps(references, indent=2) + '\n')
print(json.dumps(references, indent=2))
