"""Run the production weather parser under host memory/UB sanitizers."""
from pathlib import Path
import json
import os
import subprocess

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/espdl-quickapp'
desktop = delivery / 'overlay/apps/system/desktop'
cjson = Path('/tmp/v3-desktop-espdl-20260915/apps/netutils/cjson/cJSON')
build = Path('/tmp/p4-weather-tests')
build.mkdir(exist_ok=True)
test = ws / 'diagnostics/weather-tests/test_weather.c'
exe = build / 'test-weather'
subprocess.run(['gcc', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I' + str(desktop), '-I' + str(cjson), str(test),
                str(desktop / 'glass_weather_parse.c'), str(cjson / 'cJSON.c'),
                '-lm', '-o', str(exe)], check=True)
result = subprocess.run([str(exe), str(delivery / 'evidence/weather-api-live.json')],
                         check=True, capture_output=True, text=True)
print(result.stdout)
stubs = build / 'include'
(stubs / 'nuttx').mkdir(parents=True, exist_ok=True)
(stubs / 'netutils').mkdir(exist_ok=True)
(stubs / 'nuttx/config.h').write_text('''#pragma once
#define CONFIG_NET_IPv4 1
#define CONFIG_LIBC_NETDB 1
#define OK 0
#define ERROR -1
''')
(stubs / 'nuttx/compiler.h').write_text('''#pragma once
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
root = Path('/tmp/v3-desktop-espdl-20260915')
http_exe = build / 'test-weather-http'
subprocess.run(['gcc', '-D_GNU_SOURCE', '-g', '-O1', '-fsanitize=address,undefined',
    '-include', str(stubs / 'nuttx/compiler.h'),
    '-fno-omit-frame-pointer', '-I' + str(stubs), '-I' + str(desktop),
    '-I' + str(root / 'apps/include'), '-I' + str(cjson),
    str(ws / 'diagnostics/weather-tests/test_weather_http.c'),
    str(desktop / 'glass_weather_http.c'), str(desktop / 'glass_weather_parse.c'),
    str(root / 'apps/netutils/webclient/webclient.c'),
    str(root / 'apps/netutils/netlib/netlib_parseurl.c'), str(cjson / 'cJSON.c'),
    '-lm', '-o', str(http_exe)], check=True)
http = subprocess.run([str(http_exe)], capture_output=True, text=True)
if http.returncode:
    print(http.stdout[-1500:])
    print(http.stderr)
    http.check_returncode()
print(http.stdout.splitlines()[-1])
(delivery / 'evidence/weather-host-validation.json').write_text(json.dumps({
    'production_parser_asan_ubsan': True,
    'output': result.stdout,
    'production_http_with_real_nuttx_webclient_asan_ubsan': True,
    'http_output': http.stdout,
}, indent=2) + '\n')
