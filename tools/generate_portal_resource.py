#!/usr/bin/env python3
"""Embed the device configuration page byte-for-byte in the firmware."""
from pathlib import Path

desktop = Path(__file__).resolve().parents[1] / 'app/espdl-quickapp/overlay/apps/system/desktop'
assets = [('index.html', 'text/html'), ('style.css', 'text/css'),
          ('app.js', 'text/javascript')]
lines = ['#include <stddef.h>', '#include <string.h>']
for index, (name, _) in enumerate(assets):
    data = (desktop / 'portal' / name).read_bytes()
    lines.append(f'static const unsigned char asset{index}[]={{' +
                 ','.join(str(value) for value in data) + '};')
lines.append('const unsigned char *portal_asset(const char *path,size_t *size,const char **type) {')
for index, (name, mime) in enumerate(assets):
    condition = f'!strcmp(path,"/{name}")'
    if index == 0:
        condition += '||!strcmp(path,"/")'
    lines.append(f'if({condition}) {{ *size=sizeof(asset{index}); '
                 f'*type="{mime}; charset=utf-8"; return asset{index}; }}')
lines.append('return NULL; }')
(desktop / 'glass_portal_resource.c').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('PORTAL_ASSETS_OK')
