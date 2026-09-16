"""Exercise production ESPClaw/config/webclient/shared TLS against local TLS peers."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from socketserver import BaseRequestHandler, ThreadingTCPServer
import json
import os
import socket
import ssl
import subprocess
import tempfile
import threading
import time
from prepare_tls import TLS_THREAD_CFLAGS
from prepare_qpk_guide import prepare_qpk_guide
from qpk_test_build import qpk_test_inputs

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/espdl-quickapp'
overlay = delivery / 'overlay/apps/system'
build = Path('/tmp/v3-desktop-espdl-20260915')
core = build / 'apps/system/espclaw'
prepare_qpk_guide(overlay)
qpk_flags, qpk_sources, qpk_library = qpk_test_inputs(build, overlay)
tls = build / 'standalone-tls'
cache = Path('/tmp/p4-shared-https-tests')
tls_build = cache / 'tls-build'
cache.mkdir(exist_ok=True)
errors = []
requests = []


class Peer(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *_):
        pass

    def respond(self, status, body, headers=()):
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def stream(self, events, delay=0):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream; charset=utf-8')
        self.send_header('Transfer-Encoding', 'chunked')
        self.send_header('Connection', 'close')
        self.end_headers()
        for event in events:
            payload = ('data: ' + (event if isinstance(event, str) else json.dumps(event, ensure_ascii=False)) + '\n\n').encode()
            # Split a single SSE event across HTTP chunks, including UTF-8.
            for offset in range(0, len(payload), 7):
                chunk = payload[offset:offset+7]
                self.wfile.write(f'{len(chunk):x}\r\n'.encode() + chunk + b'\r\n')
            self.wfile.flush()
            if delay: time.sleep(delay)
        self.wfile.write(b'0\r\n\r\n'); self.wfile.flush()
        self.close_connection = True

    def do_GET(self):
        try:
            assert self.path in ('/probe', '/slow')
            if self.path == '/slow':
                time.sleep(7)
            self.respond(200, b'shared-https-ok')
        except (OSError, ssl.SSLError):
            pass  # A timeout/cancel deliberately closes the peer.
        except Exception as error:
            errors.append(str(error))

    def do_POST(self):
        try:
            assert self.request_version == 'HTTP/1.1'
            assert self.headers.get('Accept-Encoding') == 'identity'
            assert self.headers.get('Content-Type') == 'application/json'
            assert self.headers.get('Accept') in ('application/json', 'text/event-stream, application/json')
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            requests.append((self.connection.version(), self.path))
            if self.path.startswith('/v1/'):
                assert body['model'] == 'fixture-model'
                assert body['messages'][-1]['role'] == 'user'
                assert body['messages'][-1]['content'] in ('hi', '你好 hi') or self.path.endswith('/messages')
                reply = '你好，HTTPS 已连接。\r\nOK'
                if self.path == '/v1/chat/completions':
                    assert self.headers.get_all('Authorization') == ['Bearer fixture-key']
                    assert self.headers.get('x-api-key') is None
                    response = [{'choices': [{'delta': {'reasoning_content': '先检查连接。'}}]},
                                {'choices': [{'delta': {'content': reply}, 'finish_reason': 'stop'}]}, '[DONE]']
                else:
                    assert self.path == '/v1/messages'
                    assert self.headers.get_all('x-api-key') == ['fixture-key']
                    assert self.headers.get('Authorization') is None
                    assert self.headers.get('anthropic-version')
                    response = [{'type': 'message_start', 'message': {'usage': {'output_tokens': 1}}},
                                {'type': 'content_block_start', 'index': 0, 'content_block': {'type': 'text', 'text': ''}},
                                {'type': 'content_block_delta', 'index': 0, 'delta': {'type': 'text_delta', 'text': reply}},
                                {'type': 'content_block_stop', 'index': 0},
                                {'type': 'message_delta', 'delta': {'stop_reason': 'end_turn'}, 'usage': {'output_tokens': 20}},
                                {'type': 'message_stop'}]
                assert body['stream'] is True
                self.stream(response)
            elif self.path == '/sse':
                events = [{'choices': [{'delta': {'content': '中'}}]} for _ in range(12)]
                events += [{'choices': [{'delta': {}, 'finish_reason': 'stop'}]}, '[DONE]']
                self.stream(events, .1)
            elif self.path == '/sse-broken':
                self.stream([{'choices': [{'delta': {'content': 'partial'}}]}])
            elif self.path in ('/401', '/429'):
                self.respond(int(self.path[1:]), b'private-provider-body')
            elif self.path == '/redirect':
                self.respond(302, b'', [('Location', 'https://must-not-follow.invalid/')])
            elif self.path == '/gzip':
                self.respond(200, b'not-json', [('Content-Encoding', 'gzip')])
            elif self.path == '/oversize':
                self.respond(200, b'x' * (128 * 1024 + 1))
            elif self.path == '/chunked':
                self.send_response(200)
                self.send_header('Transfer-Encoding', 'chunked')
                self.send_header('Connection', 'close')
                self.end_headers()
                self.wfile.write(b'6\r\n{"ok":\r\n5\r\ntrue}\r\n0\r\n\r\n')
                self.close_connection = True
            else:
                raise AssertionError('Unexpected fixture path')
        except (OSError, ssl.SSLError):
            pass
        except Exception as error:
            errors.append(str(error))
            self.close_connection = True


class Plaintext(BaseRequestHandler):
    def handle(self):
        self.request.sendall(b'HTTP/1.1 400 Bad Request\r\n\r\n')


def checked(command, **kwargs):
    result = subprocess.run(command, text=True, capture_output=True, **kwargs)
    if result.returncode:
        print(result.stdout[-3000:])
        print(result.stderr[-6000:])
        result.check_returncode()
    return result


with tempfile.TemporaryDirectory(prefix='fixture-', dir=cache) as temp:
    root = Path(temp)
    for name in ('ca', 'wrong-ca'):
        checked(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
                 '-keyout', str(root / (name + '.key')), '-out', str(root / (name + '.pem')),
                 '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'])
    tls_flags = ' '.join([*TLS_THREAD_CFLAGS, '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    tls_stamp = tls_build / 'host-cflags.txt'
    if not (tls_build / 'library/libmbedtls.a').exists() or not tls_stamp.exists() or tls_stamp.read_text() != tls_flags:
        checked(['cmake', '-S', str(tls), '-B', str(tls_build), '-DCMAKE_BUILD_TYPE=Debug',
                 '-DCMAKE_C_FLAGS=' + tls_flags,
                 '-DENABLE_TESTING=OFF', '-DENABLE_PROGRAMS=OFF', '-DMBEDTLS_FATAL_WARNINGS=OFF',
                 '-DGEN_FILES=ON', '-DDISABLE_PACKAGE_CONFIG_AND_INSTALL=ON'])
        checked(['cmake', '--build', str(tls_build), '-j8'])
        tls_stamp.write_text(tls_flags)

    stubs = root / 'include'
    (stubs / 'nuttx').mkdir(parents=True)
    (stubs / 'netutils').mkdir()
    (stubs / 'nuttx/config.h').write_text('''#pragma once
#define CONFIG_NET_IPv4 1
#define CONFIG_LIBC_NETDB 1
#define CONFIG_WEBCLIENT_MAXHOSTNAME 128
#define CONFIG_WEBCLIENT_MAXFILENAME 2048
#define CONFIG_WEBCLIENT_MAXHTTPLINE 1024
#define OK 0
#define ERROR -1
''')
    (stubs / 'nuttx/compiler.h').write_text('''#pragma once
#include <stddef.h>
#define FAR
#define CODE
#define UNUSED(x) (void)(x)
#define nitems(x) (sizeof(x)/sizeof((x)[0]))
''')
    (stubs / 'nuttx/version.h').write_text('#define CONFIG_VERSION_MAJOR 0\n#define CONFIG_VERSION_MINOR 0\n')
    (stubs / 'debug.h').write_text('''#pragma once
#include <assert.h>
#include <nuttx/compiler.h>
#define DEBUGASSERT(x) assert(x)
#define ninfo(...) ((void)0)
#define nerr(...) ((void)0)
#define nwarn(...) ((void)0)
''')
    (stubs / 'netutils/netlib.h').write_text('''#pragma once
#include <nuttx/compiler.h>
#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
struct url_s {char *scheme; int schemelen; char *host; int hostlen; uint16_t port; char *path; int pathlen;};
int netlib_parseurl(const char *, struct url_s *);
''')
    cjson = build / 'apps/netutils/cjson/cJSON'
    includes = [stubs, overlay / 'desktop', overlay / 'espclaw/include', core / 'include',
                core / 'core', build / 'apps/include', cjson, tls / 'include',
                tls / 'tf-psa-crypto/include', tls / 'tf-psa-crypto/drivers/builtin/include',
                build / 'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/esp_common/include']
    sources = [f for f in (core / 'core').glob('*.c') if f.name not in ('claw_task.c', 'claw_paths.c')]
    sources += list((core / 'core/llm/backends').glob('*.c')) + list((core / 'utils').glob('*.c'))
    sources += [core / name for name in ('core/llm/claw_llm_runtime.c', 'core/llm/media/claw_media_pipeline.c',
                                        'port/queue.c', 'port/mutex.c', 'port/task.c', 'port/esp_err_to_name.c')]
    sources += [overlay / 'espclaw/port' / name for name in ('espclaw_chat.c', 'espclaw_main.c',
                                                           'tls_mbedtls.c', 'http_webclient.c')]
    sources += [overlay / 'desktop' / name for name in ('glass_https.c', 'glass_chat_store.c',
                'glass_chat_service.c', 'glass_portal_config.c', 'qpk_storage.c')]
    sources += [build / 'apps/netutils/webclient/webclient.c', build / 'apps/netutils/netlib/netlib_parseurl.c',
                cjson / 'cJSON.c', ws / 'diagnostics/chat-tests/https_test.c']
    sources += qpk_sources
    binary = root / 'test-https'
    checked(['cc', '-D_GNU_SOURCE', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-pthread', *TLS_THREAD_CFLAGS, '-include', str(stubs / 'nuttx/compiler.h'),
             *qpk_flags, f'-DQPK_BUILDER_ROOT="{root}/qpk"',
             f'-DPORTAL_CONFIG_ROOT="{root}/config"', *['-I' + str(p) for p in includes],
             *map(str, sources), str(tls_build / 'library/libmbedtls.a'),
             str(tls_build / 'library/libmbedx509.a'),
             str(tls_build / 'tf-psa-crypto/core/libtfpsacrypto.a'), str(qpk_library), '-lm', '-o', str(binary)])

    servers = []
    try:
        for version in (ssl.TLSVersion.TLSv1_3, ssl.TLSVersion.TLSv1_2):
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = context.maximum_version = version
            context.load_cert_chain(root / 'ca.pem', root / 'ca.key')
            if version == ssl.TLSVersion.TLSv1_3:
                context.num_tickets = 2
            server = ThreadingHTTPServer(('127.0.0.1', 0), Peer)
            server.daemon_threads = True
            server.socket = context.wrap_socket(server.socket, server_side=True)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            servers.append(server)
        server = ThreadingTCPServer(('127.0.0.1', 0), Plaintext)
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
        servers.append(server)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
        result = checked([str(binary), *[str(s.server_port if hasattr(s, 'server_port') else s.server_address[1]) for s in servers],
                          str(root / 'ca.pem'), str(root / 'wrong-ca.pem')], env=env, timeout=40)
        assert not errors, errors
        assert requests.count(('TLSv1.3', '/v1/chat/completions')) == 2
        assert requests.count(('TLSv1.3', '/v1/messages')) == 2
        print(result.stdout)
        report = {'production_sources_asan_ubsan': True,
                  'tls': 'real mbedTLS from the firmware source, local OpenSSL TLS 1.2/1.3 peers',
                  'external_ai_tested': False, 'requests': requests, 'output': result.stdout.strip()}
        (delivery / 'evidence/shared-https-validation.json').write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
