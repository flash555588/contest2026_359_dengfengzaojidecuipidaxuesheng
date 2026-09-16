"""Use a board QR capture to inspect or update AI settings without reading its key."""
from pathlib import Path
from urllib.parse import urlsplit
from urllib.request import Request, build_opener, HTTPRedirectHandler
import argparse
import cv2
import json
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('capture', type=Path)
parser.add_argument('--device', required=True, help='Expected device IPv4 address')
parser.add_argument('--backend', choices=['openai_compatible', 'anthropic_compatible'])
args = parser.parse_args()
data, _, _ = cv2.QRCodeDetector().detectAndDecode(cv2.imread(str(args.capture)))
parts = urlsplit(data)
assert parts.scheme == 'http' and parts.hostname == args.device and parts.port == 8080
assert not parts.username and not parts.password and not parts.query and parts.path == '/'
assert re.fullmatch(r'[0-9]{6}', parts.fragment), 'No device pairing code found'
origin = f'http://{args.device}:8080'


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


opener = build_opener(NoRedirect)


def request(path, body=None, token=None):
    headers = {'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    req = Request(origin + path, data=None if body is None else json.dumps(body).encode(), headers=headers)
    with opener.open(req, timeout=10) as response:
        return json.load(response)


token = request('/api/pair', {'code': parts.fragment})['token']
before = request('/api/config?section=ai', token=token)
assert 'api_key' not in before, 'Device unexpectedly returned a private key'
if args.backend:
    request('/api/config?section=ai', {'backend': args.backend}, token)
after = request('/api/config?section=ai', token=token)
assert 'api_key' not in after
if args.backend:
    assert after['backend'] == args.backend
    assert {k: v for k, v in before.items() if k != 'backend'} == {k: v for k, v in after.items() if k != 'backend'}
print(json.dumps({'backend': after['backend'], 'model': after['model'],
                  'service_host': urlsplit(after['base_url']).hostname,
                  'key_present': bool(after['api_key_set']),
                  'backend_updated': before['backend'] != after['backend']}, ensure_ascii=True))
