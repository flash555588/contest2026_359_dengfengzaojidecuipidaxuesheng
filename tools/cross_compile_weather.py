#!/usr/bin/env python3
"""Compile the weather-bearing desktop TU against the prepared P4 config.

Reads the prepared tree's flags, writes only the requested output directory.
Does not link firmware, change the prepared source tree or access hardware.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shlex
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    repo=Path(__file__).resolve().parents[1]
    desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
    env=dict(os.environ);env['PATH']='/home/flash/vela-p4/toolchains/gcc14/bin:'+env['PATH']
    cmd=['make','-s','-C',str(a.baseline/'apps/system/desktop'),
         'APPDIR='+str(a.baseline/'apps'),'TOPDIR='+str(a.baseline/'nuttx'),
         'CROSSDEV=/home/flash/vela-p4/toolchains/gcc14/bin/riscv32-esp-elf-',
         '--eval=weather-flags:;$(info WEATHER_CC=$(CC))$(info WEATHER_CFLAGS=$(CFLAGS))','weather-flags']
    result=subprocess.run(cmd,env=env,capture_output=True,text=True)
    (a.output/'flags.log').write_text(result.stdout+result.stderr)
    result.check_returncode()
    def flags(prefix):
        return shlex.split(next(line[len(prefix):] for line in result.stdout.splitlines() if line.startswith(prefix)))
    cc=flags('WEATHER_CC=');options=flags('WEATHER_CFLAGS=')
    source=desktop/'desktop_main.c'
    command=[*cc,'-I'+str(repo/'app/bilibili_live'),*options,'-I'+str(desktop),'-I'+str(a.baseline/'apps/system/desktop'),
             '-c',str(source),'-o',str(a.output/'desktop_main.c.o')]
    (a.output/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (a.output/'compile.log').open('w') as log:
        result=subprocess.run(command,cwd=a.baseline/'apps/system/desktop',env=env,stdout=log,stderr=subprocess.STDOUT)
        if not result.returncode:
            result=subprocess.run([*cc,*options,'-I'+str(desktop),'-c',str(desktop/'qpk_mjpeg.c'),
                '-o',str(a.output/'qpk_mjpeg.c.o')],cwd=a.baseline/'apps/system/desktop',env=env,stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:
        print((a.output/'compile.log').read_text());raise SystemExit(result.returncode)
    names=['desktop_main.c','glass_home_ui.inc','glass_ui.inc','glass_weather_ui.inc','glass_weather_modern.inc',
           'glass_weather_scene.inc','glass_weather_art.inc','qpk_mjpeg.c','qpk_mjpeg.h']
    records={name:hashlib.sha256((desktop/name).read_bytes()).hexdigest() for name in names}
    (a.output/'validation.json').write_text(json.dumps({'target':'ESP32-P4 RV32',
        'object_compile_only':True,'firmware_linked':False,'flashed':False,'sources':records},indent=2)+'\n')
    print('WEATHER_RV32_COMPILE_OK desktop_main.c + qpk_mjpeg.c; no flash')


if __name__=='__main__':main()
