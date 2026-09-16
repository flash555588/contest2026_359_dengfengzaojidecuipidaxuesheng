"""Embed only the small device UI; decompression library is loaded from CDN."""
import json
from pathlib import Path

def prepare_portal(desktop):
    output = '#include <stddef.h>\n#include <string.h>\n'
    assets = [('index.html', 'text/html; charset=utf-8'), ('style.css', 'text/css; charset=utf-8'), ('app.js', 'text/javascript; charset=utf-8')]
    for i, (name, _) in enumerate(assets):
        data = (desktop / 'portal' / name).read_bytes()
        output += f'static const unsigned char asset{i}[]={{' + ','.join(map(str, data)) + '};\n'
    output += 'const unsigned char *portal_asset(const char *path,size_t *size,const char **type) {\n'
    for i, (name, mime) in enumerate(assets):
        match = f'!strcmp(path,"/{name}")' + ('||!strcmp(path,"/")' if i == 0 else '')
        output += f'if({match}) {{ *size=sizeof(asset{i}); *type={json.dumps(mime)}; return asset{i}; }}\n'
    output += 'return NULL; }\n'
    (desktop / 'glass_portal_resource.c').write_text(output)

if __name__ == '__main__':
    prepare_portal(Path(__file__).resolve().parent.parent / '04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop')
