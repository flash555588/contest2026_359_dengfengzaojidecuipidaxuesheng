#!/usr/bin/env python3
"""Exercise production image encoding and HTTPS vision adapter without a model."""
import argparse
import base64
import os
from pathlib import Path
import struct
import subprocess
import zlib

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--apps', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
desktop = repo / 'app/espdl-quickapp/overlay/apps/system/desktop'
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
(out / 'netutils').mkdir(exist_ok=True)
(out / 'netutils/webclient.h').write_text('''#pragma once
#include <stdbool.h>
#include <stddef.h>
#define WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1 1
struct webclient_tls_ops {int unused;};
struct webclient_tls_connection;
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
header = bytearray(54)
header[:2] = b'BM'
for offset, value in [(2,70),(10,54),(14,40),(18,2),(22,2)]:
    struct.pack_into('<I', header, offset, value)
struct.pack_into('<HH', header, 26, 1, 24)
# Bottom row blue/white, top row red/green, row padding included.
(out / 'rgb24.bmp').write_bytes(header + bytes([255,0,0,255,255,255,0,0,0,0,255,0,255,0,0,0]))
header565 = bytearray(66)
header565[:54] = header
for offset, value in [(2,66+512*288*2),(10,66),(18,512),(22,(-288)&0xffffffff),(30,3),(54,0xf800),(58,0x07e0),(62,31)]:
    struct.pack_into('<I', header565, offset, value)
struct.pack_into('<H', header565, 28, 16)
(out / 'rgb565.bmp').write_bytes(header565 + struct.pack('<H',0xf800)*512*288)
(out / 'truncated.bmp').write_bytes(header + bytes(12))
bad = bytearray(header);struct.pack_into('<I',bad,18,0x7fffffff)
(out / 'oversized.bmp').write_bytes(bad + bytes(16))
(out / 'not-image.txt').write_bytes(b'not an image'*10)
link = out / 'symlink.bmp'
if link.is_symlink():link.unlink()
link.symlink_to(out / 'rgb24.bmp')
cjson = a.apps / 'netutils/cjson/cJSON'
flags = ['-std=gnu11','-D_GNU_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
         '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-pthread']
sources = [repo/'app/espdl-quickapp/tests/vision_host.c', desktop/'glass_chat_store.c', desktop/'glass_agent_config.c', cjson/'cJSON.c']
subprocess.run(['gcc',*flags,'-I'+str(out),'-I'+str(desktop),'-I'+str(cjson),
                '-I'+str(desktop.parent/'espclaw/include'), *map(str,sources), '-Wl,--gc-sections','-o',str(out/'vision-host')],check=True)
subprocess.run([str(out/'vision-host'),str(out)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
for name, dimensions in [('rgb24',(2,2)),('rgb565',(320,180))]:
    data = base64.b64decode((out/(name+'.b64')).read_bytes(), validate=True)
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    (out/(name+'.png')).write_bytes(data)
    pos=8;compressed=b''
    while pos<len(data):
        n=struct.unpack_from('>I',data,pos)[0];kind=data[pos+4:pos+8];chunk=data[pos+8:pos+8+n]
        assert zlib.crc32(kind+chunk)==struct.unpack_from('>I',data,pos+8+n)[0]
        if kind==b'IHDR':assert struct.unpack_from('>II',chunk)==dimensions
        if kind==b'IDAT':compressed+=chunk
        pos+=n+12
    assert pos==len(data)
    raw=zlib.decompress(compressed);w,h=dimensions
    assert len(raw)==h*(1+w*3)
    if name=='rgb24':assert raw==bytes([0,255,0,0,0,255,0,0,0,0,255,255,255,255])
    else:assert raw==(b'\x00'+b'\xff\x00\x00'*w)*h
print('VISION_PNG_OK: independently decoded PNG checksums, DEFLATE, row orientation, RGB/BGR, RGB565, aspect-preserving scale and pixel values')
