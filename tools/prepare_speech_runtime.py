#!/usr/bin/env python3
"""Fetch pinned speech dependencies and prepare a separate SD deployment tree.

No board access. C inference sources and headers retain their upstream licence.
Chinese G2P runs in its own bounded QuickJS runtime, using upstream dictionaries.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import lzma
from pathlib import Path
import re
import shutil
import struct
import urllib.request


def prepare(cache, output):
    repo = Path(__file__).resolve().parents[1]
    lock = json.loads((repo / 'app/espdl-quickapp/speech/upstream.lock.json').read_text())
    output.mkdir(parents=True, exist_ok=True)

    def fetch(entry):
        target = cache / entry['cache'] / entry['path']
        def valid(data):
            return len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256']
        if not target.exists() or not valid(target.read_bytes()):
            url = f"https://raw.githubusercontent.com/{entry['repo']}/{entry['revision']}/{entry['path']}"
            data = urllib.request.urlopen(url, timeout=60).read()
            if not valid(data):
                raise ValueError('Dependency hash mismatch: ' + entry['path'])
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        return target

    with ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(fetch, lock['sources']))
    vendor = output / 'vendor'
    for entry in lock['sources']:
        src = cache / entry['cache'] / entry['path']
        if entry['cache'] == 'sanoTTS' and entry['path'].startswith(('mcu/src/', 'mcu/include/')):
            target = vendor / src.name
        elif entry['cache'] == 'esp-sr-2.4.6' and entry['path'].startswith(('include/', 'src/')):
            target = vendor / src.name
        elif entry['cache'] == 'esp-sr-2.4.6' and entry['path'].startswith('lib/'):
            target = vendor / 'lib' / src.name
        elif entry['cache'] == 'esp-dl' and entry['path'].startswith('tools/dl_fft/'):
            target = vendor / 'fft' / Path(entry['path']).relative_to('tools/dl_fft')
        else:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)

    sd = output / 'sdcard/speech'
    zh = sd / 'sanotts-zh'
    zh.mkdir(parents=True, exist_ok=True)
    sano = cache / 'sanoTTS'
    voice = sano / 'web/voices/chinese-xiaoya'
    meta = json.loads((voice / 'meta.json').read_text())
    for key in ['front', 'dec']:
        raw = (voice / meta[key]).read_bytes()
        head = meta[key + '_dims']['meta_bytes']
        # Convert only storage precision; the published reference is widened fp16.
        values = struct.iter_unpack('<e', raw[head:])
        widened = raw[:head] + b''.join(struct.pack('<f', x[0]) for x in values)
        if hashlib.sha256(widened).hexdigest() != meta[key + '_widened_sha256']:
            raise ValueError('Chinese reference model conversion mismatch')
        (zh / (key + '.bin')).write_bytes(widened)
    (zh / 'phonemes.json').write_text(json.dumps(meta['phoneme_id_map'], ensure_ascii=False), encoding='utf8')
    for name in ['chars', 'phrases', 'phrase_keys']:
        packed = sano / 'pypkg/sanotts/g2p_data/zh_pinyin' / (name + '.xz')
        (zh / (name + '.tsv')).write_bytes(lzma.decompress(packed.read_bytes()))
    # Binary lookups avoid constructing a large JS Map and prefix Set on-chip.
    def table(name):
        return dict(line.split('\t', 1) if '\t' in line else (line, '')
                    for line in (zh / (name + '.tsv')).read_text(encoding='utf8').splitlines() if line)
    chars, phrases = table('chars'), table('phrases')
    for key in (zh / 'phrase_keys.tsv').read_text(encoding='utf8').splitlines():
        phrases.setdefault(key, '')
    strings, records = bytearray(b'\0'), bytearray()
    for group in [chars, phrases]:
        for key in sorted(group, key=lambda value: value.encode('utf8')):
            ko = len(strings); strings.extend(key.encode('utf8') + b'\0')
            vo = len(strings); strings.extend(group[key].encode('ascii') + b'\0')
            records.extend(struct.pack('<II', ko, vo))
    (zh / 'lexicon.bin').write_bytes(struct.pack('<4sIII', b'SNLX', len(chars), len(phrases), len(strings)) + records + strings)
    unicode = (sano / 'web/g2p-lazy/zh/unicode_tables.mjs').read_text(encoding='utf8')
    source = (sano / 'web/g2p-lazy/zh/zh_g2p.mjs').read_text(encoding='utf8')
    source = re.sub(r'import \{ CATEGORY_RANGES, DECIMAL_DIGIT_STARTS, UNIDATA_VERSION \}\s*from "./unicode_tables.mjs";', '', source)
    source = source[:source.index('/**\n * Fetch web/g2p-lazy/zh/')]
    source = re.sub(r'export \{ UNIDATA_VERSION \};', '', source)
    source = re.sub(r'\bexport ', '', source)
    start = source.index('  constructor(tables) {', source.index('class Lexicon'))
    end = source.index('\n  /** `pypinyin.seg.mmseg', start)
    source = source[:start] + '''  constructor(tables) {
    this.chars={get:key=>__sanoLookup(0,key,false)};
    this.phrases={has:key=>__sanoLookup(1,key,false)!==null,
      get:key=>{const value=__sanoLookup(1,key,false);return value===null?undefined:value?value.split(' '):[];}};
    this.prefixes={has:key=>__sanoLookup(1,key,true)};
  }
''' + source[end:]
    unicode = re.sub(r'\bexport ', '', unicode)
    (zh / 'g2p.js').write_text(unicode + '\n' + source + '\n' +
        'globalThis.sanoPhonemize=(text)=>Array.from(createZhG2P(__sanoTables).textToIds(text,__sanoIdMap).ids);\n', encoding='utf8')
    for directory in (cache / 'esp-sr-2.4.6/model').glob('*/*'):
        if directory.is_dir():
            shutil.copytree(directory, sd / 'esp-sr' / directory.name, dirs_exist_ok=True)
    licences = sd / 'licenses'
    licences.mkdir(exist_ok=True)
    for name, source in [('sanoTTS-MIT.txt', sano / 'LICENSE.MIT'), ('sanoTTS-GPL3.txt', sano / 'LICENSE'),
                         ('ESP-SR.txt', cache / 'esp-sr-2.4.6/LICENSE'), ('ESP-DL-FFT.txt', cache / 'esp-dl/LICENSE')]:
        shutil.copy2(source, licences / name)
    records = {p.relative_to(sd).as_posix(): {'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
               for p in sd.rglob('*') if p.is_file() and p.name != 'manifest.json'}
    (sd / 'manifest.json').write_text(json.dumps({'format': 1, 'language': 'zh-CN', 'files': records}, indent=2) + '\n')
    assets=[]
    for path, record in records.items():
        if path.startswith(('sanotts-zh/', 'esp-sr/')) and not path.endswith('.tsv'):
            digest=','.join('0x'+record['sha256'][i:i+2] for i in range(0,64,2))
            assets.append('{"'+path+'",'+str(record['bytes'])+',{'+digest+'}}')
    (vendor / 'speech_assets.h').write_text('/* Generated from verified, pinned upstream assets. */\n'
        'static const struct {const char *path; size_t bytes; uint8_t sha[32];} speech_assets[]={\n'+',\n'.join(assets)+'\n};\n')
    print('SPEECH_ASSETS_VERIFIED', len(records), sum(v['bytes'] for v in records.values()), output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.cache, args.output)
