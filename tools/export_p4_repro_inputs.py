#!/usr/bin/env python3
"""Export prepared P4 build inputs only; never open a serial port or flash."""
import argparse, hashlib, io, json, os, posixpath, re, tarfile
from pathlib import Path
from esp_tts_inputs import verify_prepared

BASE_DIRS = ('nuttx', 'apps', 'espdl-source', 'espdl-build', 'standalone-tls', 'tls-build-espdl')
DROP_DIRS = {'.git', '__pycache__', '.pytest_cache', 'private-rollback', 'CMakeFiles'}
DROP_SUFFIXES = {'.o', '.obj', '.pyc', '.d', '.map', '.elf', '.log'}
DROP_NAMES = {'nuttx', 'nuttx.bin', 'nuttx.hex', '.depend', 'Make.dep', 'bilibili-clean.txt',
              'bilibili-baseline.txt', 'music_credentials.h', 'music_api_key.h', '.env'}

def sha(data): return hashlib.sha256(data).hexdigest()

def export(source, folders, name, output, baseline=False):
    source=Path(source).resolve(); output=Path(output); output.mkdir(parents=True, exist_ok=True)
    archive=output/(name+'.tar.gz')
    if archive.exists(): raise ValueError('Refusing to overwrite '+str(archive))
    rows=[]; licenses=[]; rejected=[]; total=0
    def paths(folder):
        start=source/folder
        if start.is_file(): yield start; return
        if not start.is_dir(): raise ValueError('Missing required input '+str(start))
        for parent, dirs, files in os.walk(start):
            dirs[:]=sorted(d for d in dirs if d not in DROP_DIRS)
            for d in list(dirs):
                p=Path(parent)/d
                if p.is_symlink(): dirs.remove(d); yield p
            for f in sorted(files): yield Path(parent)/f
    try:
        with tarfile.open(archive,'w:gz',compresslevel=6) as tar:
            for folder in folders:
                for p in paths(folder):
                    rel=p.relative_to(source).as_posix()
                    if p.name in DROP_NAMES or p.suffix in DROP_SUFFIXES: continue
                    # NuttX/Apps archives are generated caches. Required TLS/ESP-DL
                    # archives live in their separately allowlisted input directories.
                    if baseline and rel.startswith(('nuttx/','apps/')) and p.suffix=='.a': continue
                    info=tarfile.TarInfo(name+'/'+rel);info.mode=0o755 if os.access(p,os.X_OK) else 0o644
                    if p.is_symlink():
                        target=p.resolve(strict=False)
                        if not target.is_relative_to(source): raise ValueError('External symlink '+rel)
                        link=os.path.relpath(target,p.parent).replace(os.sep,'/')
                        info.type=tarfile.SYMTYPE;info.linkname=link;info.mode=0o777;tar.addfile(info)
                        rows.append({'path':rel,'link':link});continue
                    data=p.read_bytes()
                    # Reject high-confidence live credential patterns without printing values.
                    if p.suffix in {'.h','.c','.json','.config','.txt','.md'} or p.name=='.config':
                        if re.search(rb'\bsk-[A-Za-z0-9_-]{24,}|Authorization:\s*Bearer\s+[A-Za-z0-9._-]{24,}',data):
                            rejected.append(rel);continue
                    if 'licenses' in [part.lower() for part in p.parts] or p.name.upper().startswith(('LICENSE','COPYING','NOTICE')): licenses.append(rel)
                    info.size=len(data);tar.addfile(info,io.BytesIO(data));total+=len(data)
                    rows.append({'path':rel,'bytes':len(data),'sha256':sha(data)})
            if rejected: raise ValueError('Review possible credentials in: '+', '.join(rejected))
            if not licenses: raise ValueError('No upstream licenses found')
            manifest={'format':1,'name':name,'original_root':str(source),'entries':rows,'license_files':licenses,
                      'excluded':'Generated objects/caches, local credentials, logs and flash backups; required input libraries retained.'}
            data=(json.dumps(manifest,indent=2)+'\n').encode();info=tarfile.TarInfo(name+'/BUILD-INPUT-MANIFEST.json');info.size=len(data);info.mode=0o644;tar.addfile(info,io.BytesIO(data))
    except BaseException:
        if archive.exists():archive.unlink()
        raise
    row={'name':name,'archive':archive.name,'bytes':archive.stat().st_size,
         'sha256':sha(archive.read_bytes()),'files':len(rows),'unpacked_bytes':total}
    print(json.dumps(row),flush=True);return row

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--speech-runtime',type=Path,required=True);p.add_argument('--esp-tts',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();verify_prepared(a.esp_tts)
    for f in ['nuttx/.config','tls-build-espdl/library/libmbedtls.a','espdl-build/libqpk_espdl.a']:
        if not (a.baseline/f).is_file():p.error('Incomplete prepared baseline: '+f)
    rows=[export(a.baseline,BASE_DIRS,'ble-foundations-20261025',a.output,True),
          export(a.speech_runtime,('vendor','sdcard'),'speech-port-20261015-prepared',a.output),
          export(a.esp_tts,('upstream','upstream.lock.json'),'esp-tts-20261017',a.output)]
    (a.output/'ARCHIVES.json').write_text(json.dumps(rows,indent=2)+'\n')
if __name__=='__main__':main()
