#!/usr/bin/env python3
"""Freeze current source bytes without committing or importing private logs."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    repo, out = args.repo.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = out/'source'
    source.mkdir()
    records = subprocess.check_output(['git','-C',str(repo),'ls-files','-z','--cached','--others','--exclude-standard']).decode('utf-8').split('\0')
    entries = []
    allowed = {'app','tools','chip','board','configs','quickapp','ouo'}
    for rel in sorted(set(records)):
        path = repo/rel
        if not rel or not path.is_file() or path.is_symlink():
            continue
        if '/' in rel and rel.split('/')[0] not in allowed:
            continue
        if path.name in {'.env','music_credentials.h','music_api_key.h'}:
            continue
        raw = path.read_bytes()
        target = source/rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        entries.append(dict(path=rel, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest()))
    head = subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD']).decode().strip()
    manifest = dict(head=head, scope='Current tracked and untracked source bytes plus root metadata; excludes logs, firmware archives, docs directory, symlinks and private credential headers.', files=entries)
    payload = (json.dumps(manifest,ensure_ascii=False,sort_keys=True,indent=2)+'\n').encode()
    (out/'source-manifest.json').write_bytes(payload)
    with (out/'source.tar.gz').open('wb') as stream:
        with gzip.GzipFile(fileobj=stream,mode='wb',mtime=0,compresslevel=1) as gz:
            with tarfile.open(fileobj=gz,mode='w') as tar:
                for row in entries:
                    raw = (source/row['path']).read_bytes()
                    info = tarfile.TarInfo(row['path'])
                    info.size = len(raw)
                    info.mode = 0o755 if row['path'].endswith('.sh') else 0o644
                    tar.addfile(info,io.BytesIO(raw))
    receipt = dict(source_manifest_sha256=hashlib.sha256(payload).hexdigest(),source_archive_sha256=hashlib.sha256((out/'source.tar.gz').read_bytes()).hexdigest(),files=len(entries),source_bytes=sum(x['bytes'] for x in entries),base_head=head,frozen_worktree=True,board_written=False)
    (out/'snapshot.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
