#!/usr/bin/env python3
"""Synchronize native and distributable copies of the shared-theme apps."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DESKTOP = ROOT / 'app/espdl-quickapp/overlay/apps/system/desktop'
EXAMPLES = DESKTOP.parent / 'espclaw/qpk'


def literal(source):
    return '\n'.join(json.dumps(line, ensure_ascii=False) for line in source.splitlines(True))


def replace_symbol(path, symbol, source):
    text = path.read_text(encoding='utf-8')
    pattern = r'static const char ' + re.escape(symbol) + r'\[\] =\s*\n.*?;(?=\r?\n)'
    replacement = 'static const char ' + symbol + '[] =\n' + literal(source) + '\n;'
    text, count = re.subn(pattern, lambda _: replacement, text, count=1, flags=re.S)
    if count != 1:
        raise ValueError(f'Missing generated symbol {symbol} in {path}')
    path.write_text(text, encoding='utf-8')


def main():
    for name in ('recorder', 'espdl'):
        source = (DESKTOP / name / 'app.js').read_text(encoding='utf-8')
        symbol = f'g_{name}_app_js'
        (DESKTOP / f'{name}_resource.c').write_text(
            f'/* Generated from {name}/app.js. SPDX-License-Identifier: Apache-2.0 */\n'
            f'static const char {symbol}[] =\n{literal(source)}\n;\n'
            f'const char *{name}_get_app_js(unsigned int *len) {{ *len=sizeof({symbol})-1; return {symbol}; }}\n',
            encoding='utf-8')
    catalog_path = EXAMPLES / 'catalog.json'
    catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
    for name, native, asset in (('hello', 'g_hello_qpk_js', 'qpk_asset_1'),
                                ('game2048', 'g_2048_qpk_js', 'qpk_asset_4')):
        source = (EXAMPLES / name / 'app.js').read_text(encoding='utf-8')
        replace_symbol(DESKTOP / 'desktop_main.c', native, source)
        replace_symbol(DESKTOP / 'glass_qpk_assets.inc', asset, source)
        next(item for item in catalog['examples'] if item['id'] == name)['source_sha256'] = hashlib.sha256(source.encode()).hexdigest()
    text = json.dumps(catalog, ensure_ascii=False, indent=2) + '\n'
    catalog_path.write_text(text, encoding='utf-8')
    replace_symbol(DESKTOP / 'glass_qpk_assets.inc', 'qpk_example_catalog', text)
    # The portal consumes the same palette without a separate network asset.
    header = (DESKTOP / 'glass_app_theme.h').read_text(encoding='utf-8')
    colors = re.findall(r'0x([0-9a-f]{6})', header)
    keys = ('canvas', 'paper', 'soft', 'line', 'ink', 'muted', 'accent',
            'on-accent', 'success', 'warning', 'danger')
    if len(colors) != 22:
        raise ValueError('Expected two complete shared palettes')
    def variables(values):
        return ';'.join(f'--ui-{key}:#{value}' for key, value in zip(keys, values))
    css_path = DESKTOP / 'portal/style.css'
    marker = '/* Generated shared application theme */'
    css = css_path.read_text(encoding='utf-8').split(marker)[0].rstrip()
    css += '\n' + marker + '\n:root{color-scheme:light;' + variables(colors[:11]) + '}\n'
    css += '@media(prefers-color-scheme:dark){:root{color-scheme:dark;' + variables(colors[11:]) + '}}\n'
    css += '''
:root{color:var(--ui-ink);background:var(--ui-canvas)}
.brand,.eyebrow,.upload>span{color:var(--ui-accent)}
.brand span{color:var(--ui-ink)}
p,label,.brand small,.section-title>span,.form-footer>span,.file-row small,.note,footer{color:var(--ui-muted)}
.card{background:var(--ui-paper);border-color:var(--ui-line);border-radius:18px;box-shadow:none}
nav,button,.button,.upload,.connect-icon{background:var(--ui-soft);color:var(--ui-ink)}
button,.button,input,select,textarea{border-radius:12px;min-height:44px}
nav .active{background:var(--ui-paper);color:var(--ui-accent);box-shadow:none}
button:hover,.button:hover{background:var(--ui-line)}
.primary,.primary:hover{background:var(--ui-accent);color:var(--ui-on-accent)}
input,select,textarea{background:var(--ui-soft);border-color:var(--ui-line);color:var(--ui-ink)}
input::placeholder{color:var(--ui-muted)}
input:focus,select:focus,textarea:focus{border-color:var(--ui-accent);box-shadow:none}
:focus-visible{outline-color:var(--ui-accent)}
.file-row,.upload{border-color:var(--ui-line)}
.badge{background:var(--ui-soft);color:var(--ui-success)}
.badge:before{background:var(--ui-success)}
.badge.offline{background:var(--ui-soft);color:var(--ui-muted)}
.badge.offline:before{background:var(--ui-muted)}
.text-button{background:transparent}
#status:not(:empty){background:var(--ui-soft);color:var(--ui-accent)}
#status.error{background:var(--ui-soft);color:var(--ui-danger)}
progress{accent-color:var(--ui-accent)}
'''
    css_path.write_text(css, encoding='utf-8')
    print('APP_UI_RESOURCES_OK')


if __name__ == '__main__':
    main()
