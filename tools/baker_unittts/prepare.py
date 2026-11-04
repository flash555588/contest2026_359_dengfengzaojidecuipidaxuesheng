"""Validate and safely unpack the fixed Baker research inputs, without deleting originals."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tarfile
import time
import zipfile

DATA_SHA256 = '5e771051f90732c789ad50a4a39fa5104f53b47355ec487cdd2cb7d06ee969d8'
ARCHIVES = {
    'BZNSYP.tar.bz2': (2520774542, DATA_SHA256),
    'fastspeech2_nosil_baker_ckpt_0.4.zip': (488623401, None),
    'hifigan_csmsc_ckpt_0.1.1.zip': (915227237, None),
    'baker_alignment_tone.tar.gz': (4954921, None),
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def safe_name(name: str) -> None:
    parts = name.replace('\\', '/').split('/')
    if name.startswith(('/', '\\')) or '..' in parts or any(':' in p for p in parts):
        raise ValueError('Unsafe archive member: ' + name)


def extract(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as z:
            for member in z.infolist():
                safe_name(member.filename)
                if (member.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('Archive symlink refused')
            z.extractall(destination)  # CRC is checked while each member is read.
    else:
        with tarfile.open(archive) as t:
            for member in t:
                safe_name(member.name)
                if not (member.isfile() or member.isdir()):
                    raise ValueError('Non-regular archive entry refused: ' + member.name)
                t.extract(member, destination, filter='data')


def prepare(source: Path, output: Path, wait_seconds: int = 0) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    record_path = output / 'inputs.json'
    if record_path.exists():
        raise FileExistsError('An input receipt already exists; choose a fresh output directory')
    deadline = time.monotonic() + wait_seconds
    data = source / 'BZNSYP.tar.bz2'
    while data.with_name(data.name + '.aria2').exists():
        if time.monotonic() >= deadline:
            raise RuntimeError('Baker download is still active; original files left untouched')
        time.sleep(10)
    records = {}
    for name, (size, expected) in ARCHIVES.items():
        path = source / name
        if path.stat().st_size != size:
            raise ValueError('Input size mismatch: ' + name)
        digest = sha256(path)
        if expected and digest != expected:
            raise ValueError('Input checksum mismatch: ' + name)
        print('VERIFIED', name, flush=True)
        destination = output / name.split('.')[0]
        extract(path, destination)
        records[name] = dict(path=str(path.resolve()), bytes=size, sha256=digest,
                             extracted=str(destination.resolve()),
                             authoritative_sha256=bool(expected))
        print('EXTRACTED', name, flush=True)
    record = dict(schema=1, dataset_license='Baker: non-commercial use only',
                  inputs=records, prepared_unix=time.time(), quality_passed=False)
    tmp = record_path.with_suffix('.tmp')
    tmp.write_text(json.dumps(record, indent=2), encoding='utf-8')
    os.replace(tmp, record_path)
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--wait-seconds', type=int, default=0)
    args = parser.parse_args()
    prepare(args.source, args.output, args.wait_seconds)
