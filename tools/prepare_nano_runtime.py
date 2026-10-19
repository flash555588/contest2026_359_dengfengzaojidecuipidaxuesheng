#!/usr/bin/env python3
"""Prepare the pinned official heart-nano INT8 diagnostic, without board access.

Run inside WSL/Linux: generated .incbin paths belong to the build filesystem.
The benchmark contributes input phoneme IDs only, never E12 model/audio blobs.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import tarfile
import urllib.request


def prepare(output):
    repo = Path(__file__).resolve().parents[1]
    lock = json.loads((repo/'app/espdl-quickapp/speech/nano.lock.json').read_text(encoding='utf-8'))
    output.mkdir(parents=True, exist_ok=True)

    def verified(url, target, expected):
        if target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest() == expected:
            return target.read_bytes()
        data = urllib.request.urlopen(url, timeout=60).read()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('Official nano SHA-256 mismatch: '+url)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return data

    archive = verified(lock['release_url'], output/'heartnano.tar.gz', lock['release_sha256'])
    release = output/'release';release.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:gz') as tar:
        for name, expected in lock['release_files'].items():
            members = [m for m in tar.getmembers() if Path(m.name).name == name and m.isfile()]
            if len(members) != 1:
                raise ValueError('Unexpected official release layout: '+name)
            data = tar.extractfile(members[0]).read()
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError('Official model hash mismatch: '+name)
            (release/name).write_bytes(data)
    vendor = output/'vendor';vendor.mkdir(exist_ok=True)
    for source in lock['sources']:
        verified(f"https://raw.githubusercontent.com/Ampixa/sanoTTS/{lock['revision']}/{source['path']}",
                 vendor/Path(source['path']).name, source['sha256'])
    (vendor/'nano_q8_meta.h').write_bytes((release/'nano_q8_meta.h').read_bytes())
    text=(vendor/'sanotts_bench_data.h').read_text(encoding='utf-8')
    match=re.search(r'SANOTTS_BENCH_IDS\[51\].*?\{(.*?)\};',text,re.S)
    ids=[int(x) for x in re.findall(r'\d+',match[1])]
    if len(ids)!=51 or min(ids)<0 or max(ids)>=62:
        raise ValueError('Unexpected official fixture IDs')
    generated=output/'generated';generated.mkdir(exist_ok=True)
    (generated/'nano_probe_ids.h').write_text(
        '/* Official E12 benchmark IDs only; official heart-nano E13b weights. */\n#include <stdint.h>\n'
        '#define NANO_PROBE_SEED UINT64_C(2236265385529901705)\n'
        'static const int32_t nano_probe_ids[]={'+','.join(map(str,ids))+'};\n',encoding='utf-8')
    assembly='.section .rodata\n'
    for symbol, name in [('nano_front_blob','front_q8.bin'),('nano_decoder_blob','model_q8.bin')]:
        path=(release/name).resolve().as_posix()
        if '"' in path or '\n' in path:
            raise ValueError('Unsupported assembler path')
        assembly+=f'.balign 16\n.global {symbol}\n{symbol}:\n.incbin "{path}"\n.zero 16\n'
    assembly+='.section .note.GNU-stack,"",@progbits\n'
    (generated/'nano_blobs.S').write_text(assembly,encoding='utf-8')
    (output/'provenance.json').write_text(json.dumps(lock,indent=2)+'\n',encoding='utf-8')
    print('OFFICIAL_HEART_NANO_READY',output)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    prepare(p.parse_args().output.resolve())
