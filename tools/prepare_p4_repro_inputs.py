#!/usr/bin/env python3
"""Fetch, verify and extract locked P4 build inputs; never configure/flash a board."""
import argparse, hashlib, json, os, re, tarfile, tempfile, urllib.parse, urllib.request
from pathlib import Path, PurePosixPath

LOCK=Path(__file__).resolve().parents[1]/'configs/p4-repro-20261027/inputs.lock.json'
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def relative(value):
    p=PurePosixPath(value)
    if not value or p.is_absolute() or '..' in p.parts or '\\' in value or ':' in value:
        raise ValueError('Unsafe archive path: '+value)
    if str(p)!=value.rstrip('/'):raise ValueError('Noncanonical archive path')
    return p

def read_lock(path):
    lock=json.loads(Path(path).read_text())
    if lock.get('format')!=1:raise ValueError('Unsupported build input lock')
    rows=lock.get('inputs',[])
    if sorted(row.get('role','') for row in rows)!=['baseline','esp_tts','speech_runtime']:
        raise ValueError('Expected exactly one baseline, speech runtime and ESP-TTS input')
    names=[];archives=[]
    for row in rows:
        name=relative(row['name']);archive=relative(row['archive'])
        if len(name.parts)!=1 or len(archive.parts)!=1 or not str(archive).endswith('.tar.gz'):
            raise ValueError('Input archive/root must use a single locked name')
        if not isinstance(row['bytes'],int) or not 0<row['bytes']<4*1024**3 or not re.fullmatch('[0-9a-f]{64}',row['sha256']):
            raise ValueError('Invalid input size/digest')
        names.append(str(name));archives.append(str(archive))
    if len(set(names))!=3 or len(set(archives))!=3:raise ValueError('Duplicate build inputs')
    return lock

def verify(root):
    root=Path(root).resolve();marker=json.loads((root/'BUILD-INPUT-READY.json').read_text())
    manifest=json.loads((root/'BUILD-INPUT-MANIFEST.json').read_text());overrides=marker['relocated_sha256']
    for row in manifest['entries']:
        p=root/Path(relative(row['path']))
        if not p.resolve(strict=False).is_relative_to(root):raise ValueError('Escaping input link: '+row['path'])
        if 'link' in row:
            if not p.is_symlink() or os.readlink(p)!=row['link']:raise ValueError('Changed input link: '+row['path'])
        elif not p.is_file() or digest(p)!=overrides.get(row['path'],row['sha256']):
            raise ValueError('Changed build input: '+row['path'])
    return marker

def obtain(row,cache,offline):
    cache=Path(cache);cache.mkdir(parents=True,exist_ok=True);archive=cache/relative(row['archive'])
    if archive.exists():
        if archive.stat().st_size!=row['bytes'] or digest(archive)!=row['sha256']:raise ValueError('Cached archive differs from lock: '+archive.name)
        return archive
    if offline:raise FileNotFoundError('Offline input missing: '+str(archive))
    url=row['url'];parsed=urllib.parse.urlsplit(url)
    if parsed.scheme!='https' or parsed.hostname!='github.com':raise ValueError('Expected locked GitHub release asset URL')
    with tempfile.NamedTemporaryFile(dir=cache,delete=False) as f:
        temp=Path(f.name)
        try:
            with urllib.request.urlopen(url,timeout=60) as response:
                total=0
                while True:
                    b=response.read(1024*1024)
                    if not b:break
                    total+=len(b)
                    if total>row['bytes']:raise ValueError('Input exceeds locked size')
                    f.write(b)
            f.close()
            if temp.stat().st_size!=row['bytes'] or digest(temp)!=row['sha256']:raise ValueError('Downloaded input differs from lock')
            os.replace(temp,archive)
        finally:
            if temp.exists():temp.unlink()
    return archive

def install(row,archive,destination):
    destination=Path(destination).resolve();destination.mkdir(parents=True,exist_ok=True)
    name=row['name'];relative(name)
    if len(PurePosixPath(name).parts)!=1:raise ValueError('Bundle must have one root directory')
    final=destination/name
    if final.exists():
        if verify(final)['archive_sha256']!=row['sha256']:raise ValueError('Existing inputs belong to another archive')
        return final
    if digest(archive)!=row['sha256']:raise ValueError('Archive digest mismatch')
    if not hasattr(tarfile,'data_filter'):raise RuntimeError('Python with tarfile.data_filter is required (Python 3.12+ recommended)')
    with tempfile.TemporaryDirectory(dir=destination,prefix='.p4-input-') as tmp:
        tmp=Path(tmp)
        with tarfile.open(archive,'r:gz') as tar:
            members=tar.getmembers();total=0;seen=set()
            if len(members)>150000:raise ValueError('Too many archive members')
            for member in members:
                path=relative(member.name)
                if path.parts[0]!=name or member.name in seen:raise ValueError('Unexpected or duplicate archive root')
                seen.add(member.name);total+=member.size
                if total>4*1024**3 or member.size>1024**3:raise ValueError('Archive expands beyond input budget')
                if not (member.isfile() or member.isdir() or member.issym()):raise ValueError('Unsupported archive member')
                if member.issym():
                    if member.linkname.startswith('/') or not (tmp/Path(path).parent/member.linkname).resolve().is_relative_to(tmp/name):
                        raise ValueError('Escaping archive symlink')
            tar.extractall(tmp,filter='data')
        root=tmp/name;manifest=json.loads((root/'BUILD-INPUT-MANIFEST.json').read_text());overrides={}
        for item in manifest['entries']:
            p=root/Path(relative(item['path']))
            if 'link' in item:
                if not p.is_symlink() or os.readlink(p)!=item['link']:raise ValueError('Input link mismatch')
            else:
                if digest(p)!=item['sha256']:raise ValueError('Input manifest mismatch')
                # Generated Kconfig files contain the author's prepared-root
                # path. Relocate only those files before the normal builder
                # performs its own baseline -> workspace relocation.
                if p.name.startswith('Kconfig'):
                    old=p.read_bytes();new=old.replace(manifest['original_root'].encode(),str(final).encode())
                    if new!=old:p.write_bytes(new);overrides[item['path']]=digest(p)
        marker={'archive_sha256':row['sha256'],'relocated_sha256':overrides}
        (root/'BUILD-INPUT-READY.json').write_text(json.dumps(marker,indent=2)+'\n')
        # Verify against the final prefix before publication. Links are all
        # relative; publication does not change their targets.
        verify(root);os.replace(root,final)
    return final

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--lock',type=Path,default=LOCK)
    p.add_argument('--cache',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--offline',action='store_true');a=p.parse_args();lock=read_lock(a.lock)
    result={}
    for row in lock['inputs']:
        archive=obtain(row,a.cache,a.offline);result[row['role']]=str(install(row,archive,a.output));print('INPUT_READY',row['role'],flush=True)
    (a.output/'P4-INPUTS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
