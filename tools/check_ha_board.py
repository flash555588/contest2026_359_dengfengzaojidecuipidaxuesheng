"""Inspect/apply the HA endpoint without logging its saved token."""
import argparse, http.client, json, re, time
from grok_board_io import Console

p = argparse.ArgumentParser()
p.add_argument('--board', default='192.168.31.33:8080')
p.add_argument('--url')
p.add_argument('--inspect-ui', action='store_true')
p.add_argument('--measure-response', action='store_true')
a = p.parse_args()
usb = Console()
try:
    usb.command('')
    reply = usb.command('desktop portal-pair', private=True)
    code = re.search(r'PORTAL_PAIR=(\d{6})', reply)
    if not code:
        raise RuntimeError('Pairing unavailable')
    time.sleep(1)  # A cold boot starts the portal listener asynchronously.
    def request(method, path, data=None, auth=None):
        c = http.client.HTTPConnection(a.board, timeout=20)
        headers = {'Content-Type': 'application/json'}
        if auth: headers['Authorization'] = 'Bearer ' + auth
        c.request(method, path, None if data is None else json.dumps(data), headers)
        r = c.getresponse(); status = r.status; body = json.loads(r.read()); c.close()
        return status, body
    status, data = request('POST', '/api/pair', {'code': code[1]})
    if status != 200: raise RuntimeError('Pairing failed')
    auth = data['token']; del data, reply, code
    status, data = request('GET', '/api/config?section=homeassistant', auth=auth)
    print('BEFORE', json.dumps({'http': status, 'url': data.get('url'), 'token_set': data.get('token_set')}, ensure_ascii=False), flush=True)
    if a.url:
        status, _ = request('POST', '/api/config?section=homeassistant', {'url': a.url}, auth)
        print('SAVE_HTTP', status, flush=True)
        status, data = request('GET', '/api/config?section=homeassistant', auth=auth)
        print('AFTER', json.dumps({'http': status, 'url': data.get('url'), 'token_set': data.get('token_set')}, ensure_ascii=False), flush=True)
    if a.inspect_ui:
        print(usb.command('desktop ui home'), flush=True)
        print(usb.command('desktop ui apps'), flush=True)
        print(usb.command('desktop ui click:智能家居'), flush=True)
        time.sleep(8)
        print(usb.command('desktop ui audit'), flush=True)
    if a.measure_response:
        status, config = request('GET', '/api/file?path=data/config/@2pportal/khomeassistant/value', auth=auth)
        if status != 200 or config.get('url') != 'http://124.223.30.246:8123':
            raise RuntimeError('Unexpected saved endpoint; diagnostic stopped')
        for resource in ['config', 'services', 'states']:
            c = http.client.HTTPConnection('124.223.30.246', 8123, timeout=20)
            c.request('GET', '/api/' + resource, headers={'Authorization': 'Bearer ' + config['token']})
            r = c.getresponse(); body = r.read(4 * 1024 * 1024); c.close()
            print('HA_RESPONSE', resource, 'http', r.status, 'bytes', len(body), flush=True)
            del body
        config.clear()
    auth = None
finally:
    usb.close()
