#!/usr/bin/env python3
"""Cross-compile pinned SanoTTS/ESP-SR NuttX adapters using actual board flags."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def build(apps, vendor, output, toolchain):
    repo = Path(__file__).resolve().parents[1]
    desktop = repo / 'app/espdl-quickapp/overlay/apps/system/desktop'
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['PATH'] = str(toolchain) + ':' + env['PATH']
    command = ['make', '-s', '-C', str(apps/'system/desktop'), 'APPDIR='+str(apps),
               'TOPDIR='+str(apps.parent/'nuttx'), 'CROSSDEV='+str(toolchain/'riscv32-esp-elf-'),
               '--eval=speech-flags:;$(info SPEECH_CC=$(CC))$(info SPEECH_CFLAGS=$(CFLAGS))', 'speech-flags']
    result = subprocess.run(command, env=env, capture_output=True, text=True, check=True)
    cc = shlex.split(next(x.removeprefix('SPEECH_CC=') for x in result.stdout.splitlines() if x.startswith('SPEECH_CC=')))
    flags = shlex.split(next(x.removeprefix('SPEECH_CFLAGS=') for x in result.stdout.splitlines() if x.startswith('SPEECH_CFLAGS=')))
    symbols=['heap_caps_malloc','heap_caps_calloc','heap_caps_aligned_alloc','heap_caps_free','xQueueCreateMutex','xQueueSemaphoreTake','xQueueGenericSend','vQueueDelete']
    flags += ['-D'+name+'=speech_'+name for name in symbols]
    flags += ['-I'+str(desktop), '-I'+str(desktop/'speech_port'), '-I'+str(vendor),
              '-I'+str(vendor/'fft'), '-I'+str(vendor/'fft/base'), '-I'+str(vendor/'fft/base/isa'),
              '-DGLASS_LOCAL_SPEECH', '-Dlog1pf=speech_log1pf', '-Os', '-fno-strict-aliasing']
    sources = [desktop/name for name in ['glass_speech.c','glass_speech_assets.c','glass_speech_port.c',
                                        'glass_speech_sr.c','glass_speech_tts.c']]
    sources += [vendor/name for name in ['snt_front_f32.c','snt_piperlite.c','esp_mn_speech_commands.c','esp_sr_debug.c']]
    sources += list((vendor/'fft').glob('*.c')) + list((vendor/'fft/base').glob('*.c'))
    objects, records = [], []
    for i, source in enumerate(sources):
        obj = output / f'speech-{i}.o'
        extra = []
        cmd = [*cc, *flags, *extra, '-c', str(source), '-o', str(obj)]
        result = subprocess.run(cmd, cwd=apps/'system/desktop', env=env, capture_output=True, text=True)
        (output/(source.name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:
            print(result.stderr);raise RuntimeError('Speech compile failed: '+source.name)
        objects.append(obj)
        records.append({'source':str(source), 'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
        print('SPEECH_RV32_OK', source.name, flush=True)
    archive = output/'libspeech_nuttx.a'
    if archive.exists():archive.unlink()
    subprocess.run([str(toolchain/'riscv32-esp-elf-ar'), 'rcs', str(archive), *map(str,objects)], check=True)
    libraries=output/'lib';libraries.mkdir(exist_ok=True)
    for source in (vendor/'lib').glob('*.a'):
        rename=[arg for name in symbols for arg in ['--redefine-sym',name+'=speech_'+name]]
        subprocess.run([str(toolchain/'riscv32-esp-elf-objcopy'),*rename,str(source),str(libraries/source.name)],check=True)
    (output/'compile.json').write_text(json.dumps({'sources':records,'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()},indent=2)+'\n')
    return archive


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for field in ['apps','vendor','output','toolchain']:p.add_argument('--'+field,type=Path,required=True)
    a=p.parse_args();build(a.apps.resolve(),a.vendor.resolve(),a.output.resolve(),a.toolchain.resolve())
