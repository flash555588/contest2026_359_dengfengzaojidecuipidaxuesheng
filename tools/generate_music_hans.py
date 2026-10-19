#!/usr/bin/env python3
"""Generate the small, read-only OpenCC t2s tables used by the lyric worker."""
import hashlib,json
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
root=repo/'app/espdl-quickapp/assets/opencc'
meta=json.loads((root/'UPSTREAM.json').read_text())
for name,digest in meta['sha256'].items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest
def read(name):
    return [line.split('\t') for line in (root/name).read_text(encoding='utf-8').splitlines() if line and not line.startswith('#')]
chars=sorted((ord(a),ord(b.split()[0])) for a,b in read('TSCharacters.txt'))
phrases=sorted(((ord(a[0]),a,b.split()[0]) for a,b in read('TSPhrases.txt')),key=lambda v:(v[0],-len(v[1].encode()),v[1]))
lines=['/* SPDX-License-Identifier: Apache-2.0',
       ' * Generated from OpenCC '+meta['commit']+'. See assets/opencc/LICENSE. */',
       '#pragma once','static const struct { uint32_t from,to; } music_hans_chars[]={']
lines+=['  {0x%x,0x%x},'%(a,b) for a,b in chars];lines+=['};',
    'static const struct { uint32_t first; const char *from,*to; } music_hans_phrases[]={']
lines+=['  {0x%x,%s,%s},'%(first,json.dumps(a,ensure_ascii=False),json.dumps(b,ensure_ascii=False)) for first,a,b in phrases]
lines+=['};','']
(repo/'app/espdl-quickapp/overlay/apps/system/desktop/glass_music_hans_table.h').write_text('\n'.join(lines),encoding='utf-8')
print('MUSIC_HANS_TABLES',len(chars),len(phrases))
