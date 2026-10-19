#!/usr/bin/env python3
"""Run the production stream/HTTP/request lifetime code with ASan and UBSan."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--apps', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
source = repo / 'app/espdl-quickapp'
desktop = source / 'overlay/apps/system/desktop'
port = source / 'overlay/apps/system/espclaw'
upstream = a.apps / 'system/espclaw'
cjson = a.apps / 'netutils/cjson/cJSON'
a.output.mkdir(parents=True, exist_ok=True)
(a.output / 'netutils').mkdir(exist_ok=True)
(a.output / 'nuttx').mkdir(exist_ok=True)
(a.output / 'nuttx/config.h').write_text('#pragma once\n')
(a.output / 'netutils/webclient.h').write_text('''#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#define WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1 1
struct webclient_tls_connection;
struct webclient_tls_ops {
 int (*connect)(void *,const char *,const char *,unsigned,struct webclient_tls_connection **);
 ssize_t (*send)(void *,struct webclient_tls_connection *,const void *,size_t);
 ssize_t (*recv)(void *,struct webclient_tls_connection *,void *,size_t);
 int (*close)(void *,struct webclient_tls_connection *);
};
struct webclient_context {
 const char *url,*method; const char *const *headers;
 unsigned nheaders,protocol_version,timeout_sec,http_status;
 char *buffer;size_t buflen;const struct webclient_tls_ops *tls_ops;void *tls_ctx;
 int (*sink_callback)(char **,int,int,int *,void *);void *sink_callback_arg;
 int (*header_callback)(const char *,bool,void *);void *header_callback_arg;
};
void webclient_set_defaults(struct webclient_context *);
void webclient_set_static_body(struct webclient_context *,const char *,size_t);
int webclient_perform(struct webclient_context *);
''')
hal = a.apps.parent / 'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/esp_common/include'
includes = [a.output, desktop, port / 'include', upstream / 'include', upstream / 'core', hal, desktop / 'espdl_port/include', cjson]
flags = ['-std=gnu11', '-D_GNU_SOURCE', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
         '-ffunction-sections', '-fdata-sections', '-pthread', *['-I'+str(x) for x in includes]]
env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1'}
for name in ['chat_memory', 'chat_deadline', 'chat_http_memory']:
    exe = a.output / name
    sources = [source / 'tests' / (name + '_host.c')]
    if name != 'chat_memory':
        if name == 'chat_http_memory':
            sources += [port / 'port/claw_stream.c']
        sources += [cjson / 'cJSON.c']
    else:
        sources += [cjson / 'cJSON.c']
    subprocess.run(['gcc', *flags, *map(str, sources), '-Wl,--gc-sections', '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=60)
    (a.output / (name + '.log')).write_text(result.stdout + result.stderr)
    print(result.stdout, end='', flush=True)
    if result.returncode:
        print(result.stderr, flush=True)
    result.check_returncode()
