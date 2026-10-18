#!/usr/bin/env python3
"""Serve, verify and rebuild the complete, pinned JIZURA reference application."""
from functools import partial
from hashlib import sha256
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
ROOT = REPO / 'app/jizura'
SOURCE = ROOT / 'upstream'


def verify():
    lock = json.loads((ROOT / 'upstream.lock.json').read_text(encoding='utf-8'))
    for name, digest in lock['files'].items():
        path = SOURCE / name
        if not path.is_file() or sha256(path.read_bytes()).hexdigest() != digest:
            raise SystemExit('Upstream snapshot differs: ' + name)
    print(f"JIZURA_SOURCE_OK {lock['version']} {lock['commit']} {len(lock['files'])} files")
    return lock


def build(target):
    lock = verify()
    target = target.resolve()
    if target in SOURCE.parents or target == SOURCE or SOURCE in target.parents:
        raise SystemExit('Build outside the immutable upstream snapshot.')
    if target.exists() and any(target.iterdir()) and not (target / 'rebuild-verification.json').is_file():
        raise SystemExit('Choose an empty output directory or an existing verified JIZURA build.')
    target.mkdir(parents=True, exist_ok=True)
    for name in lock['files']:
        dest = target / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(SOURCE / name, dest)
    for args in [('build.py', '--dev'), ('build_ae.py',), ('build_ae.py', '--lang', 'en'),
                 ('build_cep.py',), ('build_cep.py', '--lang', 'en')]:
        subprocess.run([sys.executable, *args], cwd=target, check=True)
    editions = ['index.html', *[f'{lang}/index.html' for lang in ('en', 'zh-hant', 'zh-hans', 'ko', 'id', 'vi')]]
    matching = {name: sha256((target / name).read_bytes()).hexdigest() == lock['files'][name] for name in editions}
    (target / 'rebuild-verification.json').write_text(json.dumps(dict(commit=lock['commit'],
        browser_editions_match_upstream=matching, upstream_source_preserved=True), indent=2), encoding='utf-8')
    shutil.copyfile(ROOT / 'tests/full-browser.html', target / 'full-validation.html')
    if not all(matching.values()):
        raise SystemExit('Browser output differs from the pinned upstream release; see rebuild-verification.json')
    print('JIZURA_REBUILD_OK', matching)


def serve(port, directory):
    verify()
    handler = partial(SimpleHTTPRequestHandler, directory=str(directory.resolve()))
    with ThreadingHTTPServer(('127.0.0.1', port), handler) as server:
        print(f'JIZURA_FULL_READY http://127.0.0.1:{port}/zh-hans/', flush=True)
        server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('verify')
    serving = commands.add_parser('serve')
    serving.add_argument('--port', type=int, default=8767)
    serving.add_argument('--directory', type=Path, default=SOURCE)
    building = commands.add_parser('build')
    building.add_argument('--output', type=Path, default=REPO.parent / 'output/jizura-full-build')
    args = parser.parse_args()
    if args.command == 'verify':
        verify()
    elif args.command == 'build':
        build(args.output)
    else:
        serve(args.port, args.directory)


if __name__ == '__main__':
    main()
