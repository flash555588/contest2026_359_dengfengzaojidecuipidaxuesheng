#!/usr/bin/env python3
"""Install one built app directory into /sdcard/qpk on the attached board.

The board downloads every file from a one-shot HTTP server on this PC over
the NSH console (USB Serial/JTAG) with wget, into a hidden staging
directory, and checks each size before renaming the staging directory into
place. An existing install is kept unless --replace is given; replacement
renames the old directory aside instead of deleting it. Nothing is flashed.
"""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse, hashlib, ipaddress, json, re, secrets, shutil, socket, threading, time
import serial
from serial.tools import list_ports


def safe_name(name):
    return re.fullmatch(r'[A-Za-z0-9_.\-/]+', name) and '..' not in Path(name).parts


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app', type=Path, required=True, help='built app directory, e.g. com.netsurf.browser')
    p.add_argument('--board', required=True, help='board private IPv4 (ifconfig on the console)')
    p.add_argument('--port', default='COM23')
    p.add_argument('--http-port', type=int, default=8773)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--replace', action='store_true', help='rename an existing install aside first')
    a = p.parse_args()

    app = a.app.resolve()
    manifest = json.loads((app / 'manifest.json').read_text(encoding='utf-8'))
    package = manifest['package']
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.]{0,62}', package) or app.name != package:
        raise ValueError('app directory name must equal the manifest package')
    board = ipaddress.ip_address(a.board)
    if board.version != 4 or not board.is_private or board.is_loopback:
        raise ValueError('use the attached board private IPv4')
    ports = [v for v in list_ports.comports() if v.device == a.port]
    if len(ports) != 1 or (ports[0].vid, ports[0].pid) != (0x303a, 0x1001):
        raise ValueError('expected the ESP USB Serial/JTAG console port')
    route = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    route.connect((a.board, 9))
    host = route.getsockname()[0]
    route.close()

    names = sorted(str(f.relative_to(app).as_posix()) for f in app.rglob('*') if f.is_file())
    records = []
    for name in names:
        if not safe_name(name):
            raise ValueError('unsafe file name: ' + name)
        data = (app / name).read_bytes()
        records.append({'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})

    served = set()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            m = re.fullmatch(r'/(\d+)', self.path)
            if self.client_address[0] != a.board or not m or int(m[1]) >= len(names):
                self.send_error(404)
                return
            i = int(m[1])
            f = app / names[i]
            self.send_response(200)
            self.send_header('Content-Length', str(f.stat().st_size))
            self.end_headers()
            with f.open('rb') as stream:
                shutil.copyfileobj(stream, self.wfile)
            served.add(i)

        def log_message(self, *args):
            pass

    a.output.mkdir(parents=True, exist_ok=True)
    http = ThreadingHTTPServer((host, a.http_port), Handler)
    threading.Thread(target=http.serve_forever, daemon=True).start()
    console = serial.Serial(port=None, baudrate=115200, timeout=.08, write_timeout=3)
    console.dtr = False
    console.rts = False
    console.port = a.port
    root = '/sdcard/qpk'
    target = f'{root}/{package}'
    stage = f'{root}/.inst-{secrets.token_hex(4)}'
    report = {'published': False, 'target': target, 'staging': stage, 'files': records,
              'host': host, 'board': a.board}
    try:
        console.open()
        with (a.output / 'console.log').open('wb') as log:
            def collect(seconds):
                data = bytearray()
                until = time.monotonic() + seconds
                while time.monotonic() < until:
                    block = console.read(console.in_waiting or 1)
                    if block:
                        log.write(block)
                        log.flush()
                        data.extend(block)
                text = data.decode('utf-8', errors='replace')
                if re.search(r'PANIC|Assertion failed|access fault', text, re.I):
                    raise RuntimeError('board fault during transfer')
                return text

            def cmd(text, timeout=8):
                if len(text.encode()) >= 79:
                    raise ValueError('NSH line too long: ' + text)
                collect(.1)
                console.write(b'\x15')
                collect(.15)
                for b in (text + '\r\n').encode():
                    console.write(bytes([b]))
                    time.sleep(.008)
                data = ''
                until = time.monotonic() + timeout
                while time.monotonic() < until:
                    data += collect(.15)
                    if re.search(r'nsh>\s*(?:\x1b\[K)?', data):
                        if re.search(r'nsh:.*(?:failed|not found|invalid)', data, re.I):
                            raise RuntimeError(data)
                        return data
                raise RuntimeError('console timeout: ' + text)

            collect(.5)
            cmd('')
            if not re.search(r'/sdcard type (?:vfat|fatfs)', cmd('mount')):
                raise RuntimeError('SD is not mounted')
            listing = cmd('ls ' + root)
            exists = re.search(r'(?m)^\s*' + re.escape(package) + r'/\s*$', listing)
            if exists and not a.replace:
                raise RuntimeError(f'{target} exists and was kept; rerun with --replace')
            space = cmd('df')
            m = re.search(r'(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+/sdcard', space)
            if not m or int(m[1]) * int(m[4]) < sum(r['bytes'] for r in records) + 1024 * 1024:
                raise RuntimeError('insufficient SD space')

            cmd('mkdir ' + stage)
            cmd('cd ' + stage)
            made = set()
            for name in names:
                parent = Path(name).parent
                for sub in reversed([parent, *parent.parents]):
                    s = sub.as_posix()
                    if s != '.' and s not in made:
                        cmd('mkdir ' + s)
                        made.add(s)
            for i, record in enumerate(records):
                print('SD_TRANSFER', i + 1, len(records), record['name'], record['bytes'], flush=True)
                cmd(f'wget -o a{i}.part http://{host}:{a.http_port}/{i}',
                    max(45, record['bytes'] / 65536 + 30))
                ls = cmd(f'ls -l a{i}.part')
                if not re.search(r'\b' + str(record['bytes']) + r'\b', ls):
                    raise RuntimeError('SD download size mismatch: ' + record['name'])
                cmd(f'mv a{i}.part {record["name"]}')
            if served != set(range(len(names))):
                raise RuntimeError('incomplete transfer')
            cmd('cd /')
            if exists:
                old = f'{root}/.old-{package[-20:]}-{secrets.token_hex(3)}'
                cmd(f'mv {target} {old}')
                report['previous_kept_at'] = old
            cmd(f'mv {stage} {target}')
            report['published'] = True
            report['listing'] = cmd('ls -lR ' + target, 15)
    finally:
        console.close()
        http.shutdown()
        (a.output / 'INSTALL.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print('NATIVE_APP_INSTALLED' if report['published'] else 'NATIVE_APP_NOT_INSTALLED', target, flush=True)


if __name__ == '__main__':
    main()
