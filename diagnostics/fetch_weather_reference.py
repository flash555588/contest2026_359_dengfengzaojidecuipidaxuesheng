"""Capture the live weather schema and the verified TLS trust anchor."""
from pathlib import Path
import json
import socket
import ssl
import urllib.request

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/espdl-quickapp'
url = 'https://uapis.cn/api/v1/misc/weather?extended=true'
context = ssl.create_default_context()
with urllib.request.urlopen(url, context=context, timeout=25) as response:
    data = response.read(16385)
    assert len(data) <= 16384
    weather = json.loads(data)
    (delivery / 'evidence/weather-api-live.json').write_bytes(data)
    print(json.dumps({'status': response.status, 'headers': dict(response.headers),
                      'body': weather}, ensure_ascii=True, indent=2))
with socket.create_connection(('uapis.cn', 443), timeout=20) as raw:
    with context.wrap_socket(raw, server_hostname='uapis.cn') as tls:
        chain = tls._sslobj.get_verified_chain()
        root = chain[-1]
        info = root.get_info()
        assert info['subject'] == info['issuer'], info
        pem = root.public_bytes()
        if isinstance(pem, bytes):
            pem = pem.decode('ascii')
        certificate = delivery / 'overlay/apps/system/desktop/weather_root_ca.pem'
        certificate.write_text(pem, encoding='ascii')
        (delivery / 'evidence/weather-tls-chain.json').write_text(
            json.dumps([c.get_info() for c in chain], indent=2) + '\n')
        print('Verified TLS root:', info['subject'])
