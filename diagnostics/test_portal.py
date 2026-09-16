"""Exercise production pairing, socket recovery, config and file code with sanitizers."""
from pathlib import Path
import errno, http.client, json, os, re, select, shutil, socket, struct, subprocess, time
import urllib.error, urllib.request
from prepare_portal import prepare_portal

ws = Path(__file__).resolve().parent.parent
src = ws / '04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop'
cjson = Path('/tmp/v3-desktop-espdl-20260915/apps/netutils/cjson/cJSON')
root = Path('/tmp/portal-test-data')
if root.exists():
    assert root.resolve() == Path('/tmp/portal-test-data')
    shutil.rmtree(root)
prepare_portal(src)
subprocess.run(['cc', '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined',
    '-fno-omit-frame-pointer', '-pthread', '-Wl,--wrap=accept', '-Wl,--wrap=clock_gettime',
    '-DPORTAL_DATA_ROOT="/tmp/portal-test-data"', '-DPORTAL_CONFIG_ROOT="/tmp/portal-test-data/config"',
    '-I' + str(src), '-I' + str(cjson), str(ws / 'diagnostics/portal-tests/host.c'),
    *[str(src / f) for f in ['glass_portal_config.c', 'glass_portal_files.c',
      'glass_portal_http.c', 'glass_portal_resource.c', 'qpk_storage.c']],
    str(cjson / 'cJSON.c'), '-lm', '-o', '/tmp/test-portal'], check=True)
env = dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
proc = subprocess.Popen(['/tmp/test-portal'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, text=True, env=env)
token = ''
checks = []

def line():
    assert select.select([proc.stdout], [], [], 5)[0], 'Host control timed out'
    value = proc.stdout.readline().strip()
    assert value, 'Host stopped unexpectedly'
    return value

def control(command):
    proc.stdin.write(command + '\n')
    proc.stdin.flush()
    return line()

def request(path, method='GET', data=None, auth=True, extra=None, timeout=5):
    headers = {'Authorization': 'Bearer ' + (token if auth is True else auth)} if auth else {}
    headers.update(extra or {})
    if isinstance(data, dict):
        data = json.dumps(data).encode()
        headers['Content-Type'] = 'application/json'
    req = urllib.request.Request('http://127.0.0.1:8080' + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        with error:
            return error.code, error.read()

def pair(code):
    status, body = request('/api/pair', 'POST', {'code': code}, auth=False)
    assert status == 200, 'Pairing failed (credentials omitted)'
    value = json.loads(body)['token']
    assert re.fullmatch('[0-9a-f]{32}', value)
    return value

def healthy():
    deadline = time.monotonic() + 8
    while True:
        try:
            if request('/', auth=False, timeout=1)[0] == 200:
                return
        except (OSError, urllib.error.URLError, http.client.HTTPException):
            pass
        assert time.monotonic() < deadline, 'HTTP worker failed to recover'
        time.sleep(.1)

def disconnected(peer):
    peer.settimeout(6)
    try:
        while peer.recv(4096):
            pass
    except (ConnectionResetError, BrokenPipeError):
        pass

def upload_header(name, length):
    return (f'PUT /api/file?path=files/{name} HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n'
            f'Authorization: Bearer {token}\r\nContent-Length: {length}\r\n\r\n').encode()

try:
    code = line()
    assert re.fullmatch('[0-9]{6}', code)
    assert request('/')[0] == 200
    assert b'id="pair-code"' in request('/')[1]
    assert request('/api/config?section=ai', auth=False)[0] == 401
    assert request('/api/config?section=ai', auth=code)[0] == 401
    assert request('/api/pair', 'GET', auth=False)[0] == 400
    assert request('/api/pair', 'POST', {'code': code}, auth=False,
                   extra={'Origin': 'http://evil.example'})[0] == 400
    token = pair(code)
    assert json.loads(request('/api/config?section=ai')[1])['backend']=='openai_compatible'
    for backend in ['openai', 'anthropic']:
        assert request('/api/config?section=ai', 'POST', {'backend': backend})[0] == 400
    for backend in ['anthropic_compatible', 'openai_compatible']:
        assert request('/api/config?section=ai', 'POST', {'backend': backend})[0] == 200
        assert json.loads(request('/api/config?section=ai')[1])['backend']==backend
    voice_config = json.loads(request('/api/config?section=voice')[1])
    assert voice_config == {'app_id': '', 'secret_id_set': False, 'secret_key_set': False}
    assert request('/api/config?section=voice', 'POST', {'app_id': 'not-a-number'})[0] == 400
    voice_secrets = {'app_id': '1234567890', 'secret_id': 'host-test-secret-id',
                     'secret_key': 'host-test-secret-key'}
    assert request('/api/config?section=voice', 'POST', voice_secrets)[0] == 200
    voice_body = request('/api/config?section=voice')[1]
    assert b'host-test-secret' not in voice_body
    assert json.loads(voice_body) == {
        'app_id': '1234567890', 'secret_id_set': True, 'secret_key_set': True}
    assert request('/api/config?section=voice', 'POST', {'app_id': '1234567891'})[0] == 200
    assert json.loads(request('/api/config?section=voice')[1]) == {
        'app_id': '1234567891', 'secret_id_set': True, 'secret_key_set': True}
    assert b'host-test-only' not in request('/api/config?section=ai')[1]
    assert request('/api/config?section=ai', extra={'Origin': 'http://evil.example'})[0] == 400
    assert request('/api/config?section=ai', extra={'Host': 'evil.example'})[0] == 400
    assert control('open') == code
    for _ in range(5):
        assert control('start') == 'OK'
        assert pair(code) == token
    refreshed = control('refresh')
    assert refreshed != code and control('open') == refreshed
    assert request('/api/pair', 'POST', {'code': code}, auth=False)[0] == 401
    assert request('/api/config?section=ai')[0] == 200
    assert pair(refreshed) == token
    code = refreshed
    checks.append('six-digit exchange, short code is not a bearer, reopen/refresh preserve browsers')

    wrong = '000000' if code != '000000' else '999999'
    for _ in range(5):
        assert request('/api/pair', 'POST', {'code': wrong}, auth=False)[0] == 401
    status, body = request('/api/pair', 'POST', {'code': code}, auth=False)
    assert status == 429 and 0 < json.loads(body)['retry_after'] <= 30
    control('advance 31')
    assert pair(code) == token
    checks.append('five failed guesses trigger a recoverable 30-second cooldown')

    # Retain security/storage coverage with the new authenticated session.
    blob = bytes(range(256)) * 128
    assert request('/api/file?path=files/test.bin', 'PUT', blob)[0] == 200
    assert request('/api/file?path=files/test.bin')[1] == blob
    assert request('/api/file?path=files/test.bin', 'PUT', b'overwrite')[0] == 400
    assert request('/api/file?path=files/%2e%2e/config', 'PUT', b'bad')[0] == 400
    assert request('/api/file?path=files/link/escape', 'PUT', b'bad')[0] == 400
    # The manager covers the whole volume, not only the shared folder: an
    # application's dot directory is reachable and traversal still fails.
    for directory in ('data/qpk', 'data/qpk/.data', 'data/qpk/.data/sample'):
        assert request('/api/mkdir?path=' + directory, 'POST', {})[0] == 200
    assert request('/api/file?path=data/qpk/.data/sample/value', 'PUT', b'volume-wide')[0] == 200
    assert request('/api/file?path=data/qpk/.data/sample/value')[1] == b'volume-wide'
    assert [item['name'] for item in json.loads(request('/api/files?path=data/qpk/.data')[1])['items']] == ['sample']
    assert request('/api/file?path=data/%2e%2e/config', 'PUT', b'bad')[0] == 400
    checks.append('whole-volume browsing including dot directories, traversal still rejected')
    assert request('/api/install/begin', 'POST', {'name': 'Test app', 'package': 'org.test.portal', 'entry': 'app.js'})[0] == 200
    assert request('/api/install/commit', 'POST', {})[0] == 400
    assert not (root / 'qpk/org.test.portal').exists()
    assert request('/api/install/file?path=app.js', 'PUT', b'console.log("fixture");')[0] == 200
    assert request('/api/install/file?path=manifest.json', 'PUT', b'{}')[0] == 400
    assert request('/api/install/commit', 'POST', {})[0] == 200
    assert (root / 'qpk/org.test.portal/app.js').read_bytes() == b'console.log("fixture");'
    assert request('/api/install/begin', 'POST', {'name': 'Test', 'package': 'org.test.portal'})[0] == 400
    assert request('/api/config?section=ai', 'POST', {'timeout_ms': 1})[0] == 400
    assert request('/api/config?section=ai', 'POST', {'max_tokens': 384000, 'timeout_ms': 3600000})[0] == 200
    assert request('/api/config?section=ai', 'POST', {'max_tokens': 384001})[0] == 400
    assert request('/api/config?section=ai', 'POST', b'{"x":' + b'[' * 100 + b'0' + b']' * 100 + b'}')[0] == 400
    assert request('/api/config?section=ai', 'POST', b'{"model":"bad\x00suffix"}')[0] == 400
    with socket.create_connection(('127.0.0.1', 8080)) as peer:
        peer.sendall(b'POST /api/config?section=ai HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n')
        assert b'400' in peer.recv(2048)
    checks.append('origin/host validation, AI/voice secret preservation, path safety, byte-exact files, staged install, malformed requests')

    # Empty preconnections and partial headers must not block ready peers.
    with socket.create_connection(('127.0.0.1', 8080)) as idle, socket.create_connection(('127.0.0.1', 8080)) as slow:
        slow.sendall(b'GET / HTTP/1.1\r\n')
        started = time.monotonic()
        assert request('/api/config?section=ai')[0] == 200
        assert time.monotonic() - started < 1.5
        disconnected(idle)  # Let the actual two-second empty-socket deadline elapse.
        control('advance 6')
        disconnected(slow)
    healthy()
    checks.append('empty/partial headers are multiplexed and released on absolute deadlines')

    # Reset an upload and a download; no unfinished .upload can be published.
    peer = socket.create_connection(('127.0.0.1', 8080))
    peer.sendall(upload_header('aborted.bin', 65536) + b'x' * 8192)
    time.sleep(.05)
    peer.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
    peer.close()
    healthy()
    assert not (root / 'files/aborted.bin').exists()
    assert not (root / 'files/.upload').exists()
    with socket.create_connection(('127.0.0.1', 8080)) as peer:
        peer.sendall(upload_header('slow.bin', 1024) + b'x')
        time.sleep(.05)
        control('advance 121')
        peer.sendall(b'y')
        disconnected(peer)
    assert not (root / 'files/slow.bin').exists()
    assert not (root / 'files/.upload').exists()
    peer = socket.create_connection(('127.0.0.1', 8080))
    peer.sendall((f'GET /api/file?path=files/test.bin HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nAuthorization: Bearer {token}\r\n\r\n').encode())
    peer.recv(128)
    peer.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
    peer.close()
    healthy()
    checks.append('interrupted transfers clean up; trickling requests have a total deadline')

    faults = [errno.EINTR, errno.ECONNABORTED, errno.EMFILE, errno.ENOBUFS,
              errno.ENOMEM, errno.ENETDOWN, errno.EBADF, errno.EINVAL]
    for fault in faults:
        control(f'fault {fault}')
        healthy()
        assert request('/api/config?section=ai')[0] == 200
        assert control('start') == 'OK'
    checks.append('accept interruption/resource/network faults recover; invalid listeners automatically reopen')

    baseline_fds = len(list(Path(f'/proc/{proc.pid}/fd').iterdir()))
    baseline_threads = len(list(Path(f'/proc/{proc.pid}/task').iterdir()))
    for _ in range(100):
        assert pair(code) == token
        assert request('/')[0] == 200
        assert request('/style.css')[0] == 200
        assert request('/app.js')[0] == 200
        assert request('/api/config?section=ai')[0] == 200
    time.sleep(.25)
    assert len(list(Path(f'/proc/{proc.pid}/fd').iterdir())) == baseline_fds
    assert len(list(Path(f'/proc/{proc.pid}/task').iterdir())) == baseline_threads
    debug = control('status')
    stats = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', debug)}
    assert stats['running'] == 1 and stats['active'] == 0
    assert stats['accepts'] == stats['closes']
    assert token not in debug and code not in debug and 'Bearer' not in debug
    checks.append('100 pairing/page cycles without descriptor/thread growth; diagnostics omit credentials')

    previous = token
    control('close')
    assert request('/api/config?section=ai')[0] == 401
    assert request('/api/pair', 'POST', {'code': code}, auth=False)[0] == 410
    code = control('open')
    token = pair(code)
    assert token != previous
    assert request('/api/config?section=ai', auth=previous)[0] == 401
    control('advance 901')
    assert request('/api/config?section=ai')[0] == 401
    assert request('/api/pair', 'POST', {'code': code}, auth=False)[0] == 410
    code = control('open')
    token = pair(code)
    assert request('/api/config?section=ai')[0] == 200
    checks.append('revoke and idle expiry invalidate old sessions and allow fresh pairing')
    report = {'asan': True, 'ubsan': True, 'repeat_pairing_cycles': 100,
              'accept_faults': [errno.errorcode[x] for x in faults], 'checks': checks,
              'final_socket_counts_before_revoke': stats, 'fixture': 'local host; no user credentials'}
    (ws / '04-v3-20260913/espdl-quickapp/evidence/portal-host-validation.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('PASS: pairing/reconnect, 100 repeat connections, accept recovery, bounded I/O, auth and storage checks', flush=True)
finally:
    proc.terminate()
    _, errors = proc.communicate(timeout=10)
    if errors:
        print(errors)
    assert 'ERROR: AddressSanitizer' not in errors and 'runtime error:' not in errors
