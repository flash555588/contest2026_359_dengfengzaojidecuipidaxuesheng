#!/usr/bin/env python3
"""Compile affected sources against the prepared RV32 configuration; no flash."""
from pathlib import Path
import argparse,os,shlex,subprocess,json,hashlib
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[1];desktop=root/'app/espdl-quickapp/overlay/apps/system/desktop';a.output.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['PATH']='/home/flash/vela-p4/toolchains/gcc14/bin:'+env['PATH']
    command=['make','-s','-C',str(a.baseline/'apps/system/desktop'),'APPDIR='+str(a.baseline/'apps'),'TOPDIR='+str(a.baseline/'nuttx'),'CROSSDEV=/home/flash/vela-p4/toolchains/gcc14/bin/riscv32-esp-elf-','--eval=grok-flags:;$(info GROK_CC=$(CC))$(info GROK_CFLAGS=$(CFLAGS))','grok-flags']
    result=subprocess.run(command,env=env,capture_output=True,text=True);(a.output/'flags.log').write_text(result.stdout+result.stderr)
    if result.returncode:print(result.stdout+result.stderr);raise SystemExit(result.returncode)
    cc=shlex.split(next(l.removeprefix('GROK_CC=') for l in result.stdout.splitlines() if l.startswith('GROK_CC=')))
    flags=shlex.split(next(l.removeprefix('GROK_CFLAGS=') for l in result.stdout.splitlines() if l.startswith('GROK_CFLAGS=')))
    records=[]
    for name in ['glass_grok.c','glass_grok_state.c','glass_grok_resource.c','glass_chat_service.c','qpk_runtime.c','desktop_main.c']:
        src=desktop/name;cmd=[*cc,*flags,'-I'+str(desktop),'-I'+str(a.baseline/'apps/system/desktop'),'-c',str(src),'-o',str(a.output/(name+'.o'))]
        with (a.output/(name+'.log')).open('w') as f:r=subprocess.run(cmd,env=env,cwd=a.baseline/'apps/system/desktop',stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:print((a.output/(name+'.log')).read_text());raise SystemExit(r.returncode)
        records.append({'source':name,'sha256':hashlib.sha256(src.read_bytes()).hexdigest()});print('RV32_COMPILE_OK',name,flush=True)
    (a.output/'validation.json').write_text(json.dumps({'target':'ESP32-P4 RV32, prepared configuration','sources':records,'object_compile_only':True,'firmware_linked':False,'flashed':False},indent=2)+'\n')
if __name__=='__main__':main()
