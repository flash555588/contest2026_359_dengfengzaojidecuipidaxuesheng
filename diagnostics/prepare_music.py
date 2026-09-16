"""Collect the music decoder and inspect the reference API without logging keys."""
from pathlib import Path
import hashlib
import json
import re
import ssl
import socket
import urllib.parse
import urllib.request

ws = Path(__file__).resolve().parent.parent
out = ws / '04-v3-20260913/espdl-quickapp'
desktop = out / 'overlay/apps/system/desktop'
vendor = desktop / 'music_vendor'
vendor.mkdir(exist_ok=True)
reference = Path('D:/Project/MicroReactor/project/firmware/components/qjs_runtime/include/qpk_secrets.h')
match = re.search(r'#define\s+QPK_MUSIC_API_KEY\s+"([^"\r\n]+)"', reference.read_text(encoding='utf-8'))
assert match, 'Reference music key missing'
# Private generated header, never include the reference project's other keys.
(desktop / 'music_credentials.h').write_text('#define GLASS_MUSIC_API_KEY ' + json.dumps(match[1]) + '\n')
for name in ('minimp3.h', 'LICENSE'):
    target = vendor / name
    if not target.exists():
        target.write_bytes(urllib.request.urlopen('https://raw.githubusercontent.com/lieff/minimp3/master/' + name, timeout=30).read())
    print(name, hashlib.sha256(target.read_bytes()).hexdigest())
url = 'https://jkapi.com/api/music?' + urllib.parse.urlencode(dict(plat='qq', type='json', apiKey=match[1], name='晴天'))
try:
    with urllib.request.urlopen(url, timeout=40) as response:
        result = json.load(response)
except Exception as error:
    raise SystemExit('Music API probe failed: ' + type(error).__name__)
safe = {k:v for k,v in result.items() if k not in ('music_url', 'url') and not isinstance(v, (dict,list))}
safe['stream_host'] = urllib.parse.urlsplit(result.get('music_url','')).hostname
(out / 'evidence/music-api-probe.json').write_text(json.dumps(safe, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(safe, ensure_ascii=True))
# A local fixture for decoder validation; URL is intentionally not persisted.
if result.get('music_url'):
    with urllib.request.urlopen(result['music_url'], timeout=30) as response:
        sample = response.read(256 * 1024)
    (out / 'evidence/music-sample.mp3').write_bytes(sample)
    print('MP3 fixture bytes:', len(sample))
roots = set()
for host in filter(None, ('jkapi.com', safe['stream_host'])):
    with socket.create_connection((host, 443), timeout=20) as sock:
        with ssl.create_default_context().wrap_socket(sock, server_hostname=host) as conn:
            chain = conn._sslobj.get_verified_chain()
            roots.add(chain[-1].public_bytes())
bundle = ''.join(sorted(roots))
(desktop / 'music_root_ca.pem').write_text(bundle, encoding='ascii')
(desktop / 'music_root_ca.inc').write_text('static const unsigned char music_root_ca[] =\n' + '\n'.join(json.dumps(line) for line in bundle.splitlines(keepends=True)) + ';\n')
