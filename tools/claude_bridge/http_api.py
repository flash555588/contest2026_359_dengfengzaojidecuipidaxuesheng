"""Bounded authenticated LAN API; execution remains in the session service."""
import hmac
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
from pathlib import Path
import socket
import threading
from protocol import LIMIT, AlreadyAccepted, loads, tail

NETWORKS = tuple(ipaddress.ip_network(n) for n in ('127.0.0.0/8','10.0.0.0/8','172.16.0.0/12','192.168.0.0/16'))


class BridgeHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 16
    def __init__(self, *args, **kwargs):
        self.slots = threading.BoundedSemaphore(16)
        super().__init__(*args, **kwargs)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            request.close(); return
        try: super().process_request(request, address)
        except BaseException:
            self.slots.release(); raise

    def process_request_thread(self, request, address):
        try: super().process_request_thread(request, address)
        finally: self.slots.release()

    def handle_error(self, *_): pass  # Request details may contain private data.


class HeaderReader:
    def __init__(self, stream): self.stream, self.size, self.headers = stream, 0, True
    def readline(self, size=-1):
        value = self.stream.readline(min(size,8193) if size >= 0 else 8193)
        if self.headers:
            self.size += len(value)
            if len(value) > 8192 or self.size > 16384: raise http.client.LineTooLong('header')
            if value in (b'\r\n', b'\n'): self.headers = False
        return value
    def __getattr__(self, name): return getattr(self.stream, name)


def handler(bridge):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.0'
        def setup(self):
            self.request.settimeout(5)
            super().setup(); self.rfile = HeaderReader(self.rfile)
        def handle_one_request(self):
            try: super().handle_one_request()
            except (http.client.HTTPException, RecursionError, UnicodeError, TimeoutError, ConnectionError):
                self.close_connection = True
        def log_message(self, *_): pass
        def send_json(self, code, value):
            body = json.dumps(value, ensure_ascii=False).encode()
            self.send_response(code)
            for name, value in [('Content-Type','application/json; charset=utf-8'),('Cache-Control','no-store'),
                                ('X-Content-Type-Options','nosniff'),('X-Frame-Options','DENY'),('Referrer-Policy','no-referrer')]:
                self.send_header(name,value)
            self.send_header('Content-Length',str(len(body))); self.end_headers()
            try: self.wfile.write(body)
            except (OSError, ConnectionError): pass
        def check_origin(self):
            ip = ipaddress.ip_address(self.client_address[0])
            if not any(ip in network for network in NETWORKS): raise PermissionError('仅允许本机和私有局域网连接')
            host = self.headers.get('Host','')
            if self.headers.get_all('Host') is None or len(self.headers.get_all('Host')) != 1: raise PermissionError('Host 无效')
            name, separator, port = host.rpartition(':')
            if separator:
                if port != str(self.server.server_address[1]): raise PermissionError('Host 端口无效')
            else:
                # NuttX webclient omits the non-default port in Host. Its
                # literal-IP requests carry no browser origin/fetch metadata.
                name = host
                if name == 'localhost' or self.headers.get('Origin') is not None or any(k.lower().startswith('sec-fetch-') for k in self.headers):
                    raise PermissionError('Host 端口无效')
            if name != 'localhost':
                address = ipaddress.IPv4Address(name)
                if not any(address in network for network in NETWORKS): raise PermissionError('Host 地址无效')
            origin = self.headers.get('Origin')
            if origin and origin != 'http://' + host: raise PermissionError('来源不匹配')
            if self.headers.get('Sec-Fetch-Site') == 'cross-site': raise PermissionError('跨站请求被拒绝')
            return ip
        def do_GET(self):
            try:
                self.check_origin()
                if self.path not in ('/','/app.js','/client.js'): return self.send_json(404,{'ok':False,'error':'not_found'})
                file = Path(__file__).parents[2]/'quickapp/claude_control/client.js' if self.path=='/client.js' else Path(__file__).with_name('index.html' if self.path=='/' else 'app.js')
                page = file.read_bytes()
                self.send_response(200)
                self.send_header('Content-Type',('text/html' if self.path=='/' else 'text/javascript')+'; charset=utf-8'); self.send_header('Content-Length',str(len(page)))
                self.send_header('Cache-Control','no-store'); self.send_header('X-Frame-Options','DENY')
                self.send_header('X-Content-Type-Options','nosniff'); self.send_header('Referrer-Policy','no-referrer')
                self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
                self.end_headers(); self.wfile.write(page)
            except (ValueError, PermissionError): self.send_json(403,{'ok':False,'error':'来源无效'})
        def do_POST(self):
            try:
                ip = self.check_origin()
                if self.headers.get_content_type() != 'application/json': raise ValueError('只接受 application/json')
                lengths = self.headers.get_all('Content-Length', [])
                if len(lengths) != 1 or not lengths[0].isascii() or not lengths[0].isdigit() or self.headers.get('Transfer-Encoding'):
                    raise ValueError('请求长度无效')
                length = int(lengths[0])
                observing = self.path.startswith('/internal/observe/')
                hook = observing or self.path.startswith('/internal/permission/')
                if not 0 < length <= (1048576 if hook else LIMIT): raise ValueError('请求过大')
                action = self.path.removeprefix('/api/')
                local_pair = self.path == '/api/localpair' and ip.is_loopback
                key = self.headers.get('Authorization','').removeprefix('Bearer ')
                if hook:
                    if not ip.is_loopback: raise PermissionError()
                    ident = self.path.rsplit('/',1)[-1]
                    expected = bridge.hook_keys.get(ident)
                    if not expected or not hmac.compare_digest(key,expected): raise PermissionError()
                else:
                    if self.path != '/api/'+action: raise ValueError('接口不存在')
                    if action != 'pair' and not local_pair and not hmac.compare_digest(key,bridge.token): raise PermissionError('设备未配对')
                raw = self.rfile.read(length)
                if len(raw) != length: raise ValueError('请求不完整')
                args = loads(raw)
                if not isinstance(args,dict): raise ValueError('参数应为对象')
                if observing: value = bridge.observe(ident,key,args)
                elif hook: value = bridge.permission(ident,key,args)
                elif local_pair: value = {'ok':True,'token':bridge.token}
                else: value = bridge.action(action,args,str(ip))
                self.send_json(200,value)
            except AlreadyAccepted as exc: self.send_json(409,{'ok':False,'accepted':True,'error':str(exc)})
            except PermissionError as exc: self.send_json(403,{'ok':False,'accepted':False,'error':str(exc) or '拒绝访问'})
            except (ValueError,OSError,KeyError,TypeError,RecursionError,UnicodeError) as exc:
                self.send_json(400,{'ok':False,'accepted':None if isinstance(exc,OSError) else False,'error':tail(bridge.safe(str(exc)),160)})
    return Handler
