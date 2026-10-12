#!/usr/bin/env python3
"""Build current v3 overlay + live app using an explicit prepared baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--workspace',type=Path,required=True)
    p.add_argument('--toolchain',type=Path,required=True)
    p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--source-revision',help='Explicit 40-hex source revision for Windows-mounted Git worktrees')
    a=p.parse_args();repo=Path(__file__).resolve().parent.parent
    base=a.baseline.resolve();root=a.workspace.resolve();out=a.output.resolve()
    if root==base or base in root.parents: p.error('Use an isolated workspace outside baseline')
    if not (base/'tls-build-espdl/library/libmbedtls.a').exists():p.error('Prepared v3 TLS libraries required')
    env=os.environ.copy();env['PATH']=str(a.toolchain.resolve())+':'+str(Path.home()/'.local/bin')+':'+env['PATH']
    def run(argv,**kw):subprocess.run(list(map(str,argv)),env=env,check=True,**kw)
    first=not root.exists()
    if first:run(['cp','-a','--reflink=auto',base,root])
    marker=root/'bilibili-baseline.txt'
    if not first and (not marker.exists() or marker.read_text().strip()!=str(base)):p.error('Workspace belongs to another baseline')
    marker.write_text(str(base)+'\n')
    # Generated Kconfig files embed absolute source paths. Relocate all of
    # them before olddefconfig, otherwise options resolve in the old tree.
    for folder in ['apps','nuttx']:
        for f in (root/folder).rglob('Kconfig*'):
            if f.is_file() and not f.is_symlink():
                data=f.read_bytes();updated=data.replace(str(base).encode(),str(root).encode())
                if updated!=data:f.write_bytes(updated)
    # Apply only the requested current overlay, preserving the external tree.
    overlay=repo/'app/espdl-quickapp/overlay'
    for src in overlay.rglob('*'):
        if not src.is_file():continue
        # The shipped overlay .config predates hardware/API preparation.
        # Start from the explicitly prepared baseline's resolved configuration.
        if src.relative_to(overlay)==Path('nuttx/.config'):continue
        target=root/src.relative_to(overlay);target.parent.mkdir(parents=True,exist_ok=True)
        data=src.read_bytes()
        if src.name.startswith('Kconfig'):data=data.replace(b'\r\n',b'\n')
        if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
    # No private credential header is imported into the candidate.
    for name in ['music_credentials.h','music_api_key.h']:
        (root/'apps/system/desktop'/name).unlink(missing_ok=True)
    run(['python3',repo/'tools/install_bilibili_live.py','--apps',root/'apps','--archive',a.archive])
    run(['../tools/mkkconfig.sh','-m','System Libraries and NSH Add-Ons','-o','Kconfig'],cwd=root/'apps/system')
    import re
    cfg=root/'nuttx/.config';s=cfg.read_text() if (root/'bilibili-clean.txt').exists() else (base/'nuttx/.config').read_text()
    for name,value in [('SYSTEM_BILIBILI_LIVE','y'),('PTHREAD_MUTEX_TYPES','y'),('NSH_DISABLE_DATE','n')]:
        key='CONFIG_'+name;line=key+'='+value if value!='n' else '# '+key+' is not set'
        pattern=r'^(?:'+key+r'=.*|# '+key+r' is not set)$'
        if re.search(pattern,s,re.M):s=re.sub(pattern,line,s,flags=re.M)
        else:s+=line+'\n'
    if cfg.read_text()!=s:cfg.write_text(s)
    tc=a.toolchain.resolve();cross=f'CROSSDEV={tc}/riscv32-esp-elf-'
    tls=base/'standalone-tls'
    includes=f'-I{tls}/include -I{tls}/tf-psa-crypto/include -I{tls}/tf-psa-crypto/drivers/builtin/include -DMBEDTLS_THREADING_C -DMBEDTLS_THREADING_PTHREAD'
    abi=['-march=rv32imafc_zicsr_zifencei','-mabi=ilp32f']
    libs=[base/'espdl-build/libqpk_espdl.a',base/'espdl-build/libfbs_model_nuttx.a',
          base/'tls-build-espdl/library/libmbedtls.a',base/'tls-build-espdl/library/libmbedx509.a',
          base/'tls-build-espdl/tf-psa-crypto/core/libtfpsacrypto.a']
    for name in ['libstdc++.a','libsupc++.a','libgcc.a']:
        libs.append(Path(subprocess.check_output([str(tc/'riscv32-esp-elf-g++'),*abi,'-print-file-name='+name],text=True).strip()))
    run(['make','olddefconfig',cross],cwd=root/'nuttx')
    config=cfg.read_text()
    for value in ['CONFIG_SYSTEM_C6_DESKTOP=y','CONFIG_SYSTEM_BILIBILI_LIVE=y','CONFIG_ARCH_FPU=y',
                  'CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET=0xc00000']:
        if value not in config:raise RuntimeError('Unexpected v3 configuration: '+value)
    clean_stamp=root/'bilibili-clean.txt'
    config_hash=hashlib.sha256(config.encode()).hexdigest()
    if not clean_stamp.exists() or clean_stamp.read_text().strip()!=config_hash:
        run(['make','clean',cross],cwd=root/'nuttx');clean_stamp.write_text(config_hash+'\n')
    run(['make','context',cross,f'ESPCLAW_TLS_CFLAGS={includes}'],cwd=root/'nuttx')
    run(['make',f'-j{a.jobs}',cross,f'ESPCLAW_TLS_CFLAGS={includes}','EXTRA_LIBS='+' '.join(map(str,libs))],cwd=root/'nuttx')
    if 0x2000+(root/'nuttx/nuttx.bin').stat().st_size>0x800000:raise RuntimeError('Firmware overlaps models')
    out.mkdir(parents=True,exist_ok=True)
    for name,target in [('nuttx.bin','nuttx.bin'),('nuttx','nuttx.elf'),('.config','resolved.config')]:shutil.copy2(root/'nuttx'/name,out/target)
    def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    nm=subprocess.check_output([str(tc/'riscv32-esp-elf-nm'),'-C',str(out/'nuttx.elf')],text=True)
    for symbol in ['bili_live_show','bili_flv_run','glass_https_tls_ops','WelsCreateDecoder']:
        if symbol not in nm:raise RuntimeError('Missing linked symbol '+symbol)
    revision=a.source_revision
    if revision:
        if not re.fullmatch('[0-9a-f]{40}',revision):p.error('source-revision must be 40-hex')
    else:revision=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    metadata={'upstream':revision,
              'baseline':str(base),'baseline_config_sha256':sha(base/'nuttx/.config'),
              'workspace':str(root),'tls':'existing glass_https / mbedTLS',
              'compiler':subprocess.check_output([str(tc/'riscv32-esp-elf-gcc'),'--version'],text=True).splitlines()[0],
              'flash_offset':'0x2000','data_offset':'0xc00000','board_tested':False,
              'sha256':{name:sha(out/name) for name in ['nuttx.bin','nuttx.elf','resolved.config']},
              'reused_libraries':{str(lib):sha(lib) for lib in libs},
              'source_sha256':{str(f.relative_to(repo)):sha(f) for folder in [repo/'app/bilibili_live',overlay]
                               for f in folder.rglob('*') if f.is_file()}}
    (out/'BUILD-METADATA.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('Build verified:',out,metadata['sha256']['nuttx.bin'])
if __name__=='__main__':main()
