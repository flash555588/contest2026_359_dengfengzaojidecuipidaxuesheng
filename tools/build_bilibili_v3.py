#!/usr/bin/env python3
"""Build current v3 overlay + live app using an explicit prepared baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import re

def storage_layout(config,requested='preserve'):
    layouts={'12m-4m':(0xc00000,0x400000),'14m-2m':(0xe00000,0x200000)}
    if requested!='preserve':
        if requested not in layouts:raise ValueError('Unknown storage layout')
        return layouts[requested]
    values=[]
    for key in ['OFFSET','SIZE']:
        match=re.search(r'^CONFIG_ESPRESSIF_STORAGE_MTD_'+key+r'=(\S+)$',config,re.M)
        if not match:raise ValueError('Missing prepared storage '+key.lower())
        values.append(int(match.group(1),0))
    if tuple(values) not in layouts.values():raise ValueError('Unsupported prepared storage layout; inspect the board before changing it')
    return tuple(values)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--workspace',type=Path,required=True)
    p.add_argument('--toolchain',type=Path,required=True)
    p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--storage-layout',choices=['preserve','12m-4m','14m-2m'],default='preserve',help='Preserve the prepared data partition by default; explicit layouts do not migrate existing board data')
    p.add_argument('--l2-kb',type=int,choices=[256,512],default=256,help='P4 L2 cache experiment; 512 KiB uses 128-byte lines')
    p.add_argument('--speech-runtime',type=Path,help='Prepared, verified speech runtime directory (SD models, no flash fallback)')
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
    speech_libraries=[]
    desktop_make=root/'apps/system/desktop/Makefile'
    make=desktop_make.read_text()
    make=make.split('# BEGIN OPTIONAL LOCAL SPEECH')[0].rstrip()+'\n'
    if a.speech_runtime:
        from build_speech_runtime import build as build_speech
        prepared=a.speech_runtime.resolve()
        archive=build_speech(root/'apps',prepared/'vendor',out/'speech',a.toolchain.resolve())
        speech_libraries=[archive]+list((out/'speech/lib').glob('*.a'))
        make+='\n# BEGIN OPTIONAL LOCAL SPEECH\nCFLAGS += -DGLASS_LOCAL_SPEECH -DQPK_DL_SD_ONLY\n'
    desktop_make.write_text(make)

    # No private credential header is imported into the candidate.
    for name in ['music_credentials.h','music_api_key.h']:
        (root/'apps/system/desktop'/name).unlink(missing_ok=True)
    run(['python3',repo/'tools/install_bilibili_live.py','--apps',root/'apps','--archive',a.archive])
    run(['../tools/mkkconfig.sh','-m','System Libraries and NSH Add-Ons','-o','Kconfig'],cwd=root/'apps/system')
    import re
    cfg=root/'nuttx/.config';s=cfg.read_text() if (root/'bilibili-clean.txt').exists() else (base/'nuttx/.config').read_text()
    try:data_offset,data_size=storage_layout(s,a.storage_layout)
    except ValueError as error:p.error(str(error))
    # Live FLV needs more than the baseline's 3 KiB socket window. Keep the
    # pool bounded (48 KiB payload) and leave internal RAM for DMA/threads.
    for name,value in [('ESPRESSIF_STORAGE_MTD_OFFSET',hex(data_offset)),('ESPRESSIF_STORAGE_MTD_SIZE',hex(data_size)),('LIBM','y'),('LIBM_TOOLCHAIN','n'),('LIBM_NONE','n'),('NET_BROADCAST','y'),('SYSTEM_BILIBILI_LIVE','y'),('PTHREAD_MUTEX_TYPES','y'),('NSH_DISABLE_DATE','n'),
                       ('NET_RECV_BUFSIZE','32768'),('NET_MAX_RECV_BUFSIZE','32768'),('NSH_NETINIT','n'),
                       ('IOB_NBUFFERS','96'),('IOB_BUFSIZE','512'),('RR_INTERVAL','20'),('ESP32P4_PPA','y'),
                       ('ESPRESSIF_CACHE_L2_CACHE_128KB','n'),
                       ('ESPRESSIF_CACHE_L2_CACHE_256KB','y' if a.l2_kb==256 else 'n'),
                       ('ESPRESSIF_CACHE_L2_CACHE_512KB','y' if a.l2_kb==512 else 'n'),
                       ('ESPRESSIF_CACHE_L2_CACHE_LINE_64B','y' if a.l2_kb==256 else 'n'),
                       ('ESPRESSIF_CACHE_L2_CACHE_LINE_128B','y' if a.l2_kb==512 else 'n')]:
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
    if speech_libraries:libs=['--start-group',*speech_libraries,*libs,'--end-group']
    for name in ['libstdc++.a','libsupc++.a','libgcc.a']:
        libs.append(Path(subprocess.check_output([str(tc/'riscv32-esp-elf-g++'),*abi,'-print-file-name='+name],text=True).strip()))
    run(['make','olddefconfig',cross],cwd=root/'nuttx')
    config=cfg.read_text()
    for value in ['CONFIG_SYSTEM_C6_DESKTOP=y','CONFIG_SYSTEM_BILIBILI_LIVE=y','CONFIG_ARCH_FPU=y',
                  f'CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET={hex(data_offset)}',f'CONFIG_ESPRESSIF_STORAGE_MTD_SIZE={hex(data_size)}',
                  f'CONFIG_ESPRESSIF_CACHE_L2_CACHE_SIZE={hex(a.l2_kb*1024)}',
                  f'CONFIG_ESPRESSIF_CACHE_L2_CACHE_LINE_SIZE={64 if a.l2_kb==256 else 128}']:
        if value not in config:raise RuntimeError('Unexpected v3 configuration: '+value)
    clean_stamp=root/'bilibili-clean.txt'
    config_hash=hashlib.sha256((config+'\nspeech='+str(bool(a.speech_runtime))).encode()).hexdigest()
    if not clean_stamp.exists() or clean_stamp.read_text().strip()!=config_hash:
        run(['make','clean',cross],cwd=root/'nuttx');clean_stamp.write_text(config_hash+'\n')
    run(['make','context',cross,f'ESPCLAW_TLS_CFLAGS={includes}'],cwd=root/'nuttx')
    run(['make',f'-j{a.jobs}',cross,f'ESPCLAW_TLS_CFLAGS={includes}','EXTRA_LIBS='+' '.join(map(str,libs))],cwd=root/'nuttx')
    program_limit=data_offset
    if 0x2000+(root/'nuttx/nuttx.bin').stat().st_size>program_limit:raise RuntimeError('Firmware overlaps reserved storage')
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
              'flash_offset':'0x2000','program_limit':hex(program_limit),'vision_models_sd_only':True,'data_offset':hex(data_offset),'data_size':hex(data_size),'flash_layout':f'{data_offset//0x100000}m-firmware-{data_size//0x100000}m-data','board_tested':False,'l2_cache_kb':a.l2_kb,
              'sha256':{name:sha(out/name) for name in ['nuttx.bin','nuttx.elf','resolved.config']},
              'reused_libraries':{str(lib):sha(Path(lib)) for lib in libs if Path(lib).is_file()},
              'source_sha256':{str(f.relative_to(repo)):sha(f) for folder in [repo/'app/bilibili_live',overlay]
                               for f in folder.rglob('*') if f.is_file()}}
    if a.speech_runtime:
        metadata['speech']={'lock_sha256':sha(repo/'app/espdl-quickapp/speech/upstream.lock.json'),
                           'sd_manifest_sha256':sha(prepared/'sdcard/speech/manifest.json'),
                           'compile':json.loads((out/'speech/compile.json').read_text()),
                           'tools':{str(f.relative_to(repo)):sha(f) for f in [repo/'tools/build_bilibili_v3.py',repo/'tools/build_speech_runtime.py',repo/'tools/prepare_speech_runtime.py']}}
    (out/'BUILD-METADATA.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('Build verified:',out,metadata['sha256']['nuttx.bin'])
if __name__=='__main__':main()
