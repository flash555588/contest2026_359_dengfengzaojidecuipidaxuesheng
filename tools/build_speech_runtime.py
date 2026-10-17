#!/usr/bin/env python3
"""Cross-compile pinned SanoTTS/ESP-SR NuttX adapters using actual board flags."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import re
import shutil


def prepare_nano_storage(source, output):
    """Keep the pinned math intact; put its 82 KiB static arrays in owned RAM.

    The existing P4 image nearly fills its safe early-boot BSS region. All
    array lvalues (including sizeof and alignment) retain their exact types.
    The caller allocates before entering cancellable compute and frees after.
    """
    repo=Path(__file__).resolve().parents[1]
    lock=json.loads((repo/'app/espdl-quickapp/speech/nano.lock.json').read_text())
    expected=next(x['sha256'] for x in lock['sources'] if x['path']=='mcu/src/snt_nano.c')
    data=source.read_bytes()
    if hashlib.sha256(data).hexdigest()!=expected:
        raise ValueError('Nano storage adaptation requires the pinned upstream')
    text=data.decode('utf-8').replace('\r\n','\n')
    arrays=[]
    pattern=r'(?m)^(\s*)static (float|int|NanoScratch|nano_acc_t) ([^;\n]+);'
    def replace(match):
        indent,ctype,declarations=match.groups()
        if '[' not in declarations:return match[0]
        if declarations.startswith(('g_exp2_lut[','g_gelu_lut[','g_silu_lut[')):
            return match[0]  # Disabled optional fast-math tables stay upstream.
        replacements=[]
        for declaration in declarations.split(','):
            m=re.fullmatch(r'\s*(\w+)\s*((?:\[[^\]]+\])+)',declaration.strip())
            if not m:raise ValueError('Unsupported nano array: '+declaration)
            name,dims=m.groups();slot=len(arrays);arrays.append((ctype,name,dims))
            replacements.append(f'{indent}typedef {ctype} nano_type_{name}{dims};\n#define {name} (*(nano_type_{name} *)nano_storage[{slot}])')
        return '\n'.join(replacements)
    text=re.sub(pattern,replace,text)
    names={name for _,name,_ in arrays}
    required={'g_sin_q','g_scr','g_acc_head','g_fftw_re','g_fftw_im','durs','melring','halo','halo_next','tile_c','win','ring','fre','fim','cwre','cwim','zre','zim','env_inv','pcm_buf'}
    if names!=required:raise ValueError('Changed nano storage layout: '+str(names))
    # Leave similarly named struct fields untouched; replace only uses after
    # the original array declaration, where its lexical scope begins.
    for name in names:
        start=text.index('#define '+name+' ')
        text=text[:start]+re.sub(r'(?<![.>])\b'+name+r'\b','nano_array_'+name,text[start:])
    text=text.replace('#include "snt_nano.h"','#include "snt_nano.h"\nstatic void *nano_storage[32];')
    text=text.replace('#ifdef SNT_NANO_FAST_MATH','#ifdef SNT_NANO_FAST_MATH\n#error "Diagnostic storage adapter validates exact math only"',1)
    text=text.replace('static int tables_init = 0;','int tables_init = 0;')
    sizes=[f'((sizeof({ctype} {dims})+15u)&~15u)' for ctype,name,dims in arrays]
    text+='\nsize_t glass_nano_storage_size(void) { return '+'+'.join(sizes)+'; }\n'
    text+='void glass_nano_storage_bind(void *buffer) { unsigned char *p=buffer;\n'
    for i,size in enumerate(sizes):text+=f'nano_storage[{i}]=p;p+={size};\n'
    text+='g_sc_init=g_fftw_init=0;\n}\n'
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(text.encode('utf-8'))
    return output


def prepare_compute_sources(vendor, output):
    """Tile conv1d and add cancellation to pinned allocation-free C kernels.

    Originals stay untouched. Refuse a changed upstream file or loop layout;
    retain its licence and record both original and generated source hashes.
    """
    repo=Path(__file__).resolve().parents[1]
    lock=json.loads((repo/'app/espdl-quickapp/speech/upstream.lock.json').read_text())
    output.mkdir(parents=True,exist_ok=True)
    result={}
    for name,count in [('snt_front_f32.c',1),('snt_piperlite.c',2)]:
        records=[row for row in lock['sources'] if row['cache']=='sanoTTS' and row['path']=='mcu/src/'+name]
        source=vendor/name;data=source.read_bytes()
        if len(records)!=1 or len(data)!=records[0]['bytes'] or hashlib.sha256(data).hexdigest()!=records[0]['sha256']:
            raise ValueError('Compute instrumentation requires the pinned original: '+name)
        text=data.decode('utf-8').replace('\r\n','\n')
        include='#include "'+name.removesuffix('.c')+'.h"'
        loop='for (oc = 0; oc < out_ch; oc++) {'
        if text.count(include)!=1 or text.count(loop)!=count:
            raise ValueError('Unsupported speech compute source layout: '+name)
        text=text.replace(include,include+'\n#include "glass_speech_compute.h"')
        prefix='fr' if name=='snt_front_f32.c' else 'pl'
        start=text.index('static void '+prefix+'_conv1d(')
        end=text.index('\n}\n',start)+3
        signature=text[start:text.index('{',start)]
        args='out, x, w, b, in_ch, out_ch, T, K, '+('1, ' if prefix=='fr' else 'dil, ')+'accumulate'
        text=text[:start]+signature+'{\n    glass_speech_conv1d('+args+');\n}\n'+text[end:]
        text=text.replace(include,include+'\n#include "glass_speech_conv.h"')
        text=text.replace(loop,loop+'\n        glass_speech_compute_checkpoint();')
        target=output/name;target.write_bytes(text.encode('utf-8'))
        result[name]=target
    return result


def build(apps, vendor, output, toolchain, nano_prepared=None, zh_prepared=None, esp_tts_prepared=None, p4tts_prepared=None):
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
                                        'glass_speech_sr.c','glass_speech_tts.c','glass_speech_compute.c',
                                        'glass_custom_wake.c','glass_wake_features.c']]
    sources += [vendor/name for name in ['snt_front_f32.c','snt_piperlite.c','esp_mn_speech_commands.c','esp_sr_debug.c']]
    sources += list((vendor/'fft').glob('*.c')) + list((vendor/'fft/base').glob('*.c'))
    if nano_prepared:
        nano_lock=json.loads((repo/'app/espdl-quickapp/speech/nano.lock.json').read_text())
        nano_files=[(nano_prepared/'release'/name,digest) for name,digest in nano_lock['release_files'].items()]
        nano_files += [(nano_prepared/'vendor'/Path(row['path']).name,row['sha256'])
                       for row in nano_lock['sources'] if 'BoardBenchmark' not in row['path']]
        nano_files += [(nano_prepared/'vendor/nano_q8_meta.h',nano_lock['release_files']['nano_q8_meta.h'])]
        for path,expected in nano_files:
            if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:
                raise ValueError('Official nano build input changed: '+str(path))
        flags += ['-DGLASS_NANO_PROBE', '-I'+str(nano_prepared/'vendor'), '-I'+str(nano_prepared/'generated')]
        sources += [desktop/'glass_nano_probe.c', desktop/'glass_nano_pie.S',
                    nano_prepared/'vendor/snt_nano.c', nano_prepared/'generated/nano_blobs.S']
    compute=prepare_compute_sources(vendor,output/'compute-sources')
    if nano_prepared:
        compute['snt_nano.c']=prepare_nano_storage(nano_prepared/'vendor/snt_nano.c',output/'compute-sources/snt_nano.c')
    if zh_prepared:
        zh_manifest=json.loads((zh_prepared/'manifest.json').read_text())
        if not zh_manifest.get('diagnostic_only') or zh_manifest.get('quality_passed'):
            raise ValueError('Chinese Q8 integration currently accepts diagnostic fixtures only')
        for row in zh_manifest['sources']:
            if hashlib.sha256((zh_prepared/row['path']).read_bytes()).hexdigest()!=row['sha256']:
                raise ValueError('Chinese Q8 fixture changed: '+row['path'])
        flags += ['-DGLASS_ZH_Q8_PROBE','-I'+str(zh_prepared/'vendor'),'-I'+str(zh_prepared)]
        sources += [desktop/'glass_zh_q8.c',desktop/'glass_zh_q8_probe.c',desktop/'glass_zh_q8_pie.S',
                    zh_prepared/'generated/snt_front_q8.c',zh_prepared/'generated/snt_piperlite_q8.c',
                    zh_prepared/'blobs.S']
    esp_libraries=[]
    if p4tts_prepared:
        p4tts_manifest=json.loads((p4tts_prepared/'manifest.json').read_text())
        if hashlib.sha256((p4tts_prepared/'weights.bin').read_bytes()).hexdigest()!=p4tts_manifest['weights_sha256']:
            raise ValueError('User P4TTS diagnostic weights changed')
        flags+=['-DGLASS_P4TTS_PROBE','-DP4_TARGET','-I'+str(p4tts_prepared)]
        sources += [p4tts_prepared.parent/'p4tts_bench.c',desktop/'glass_nano_pie.S',desktop/'glass_zh_q8_pie.S']
    if esp_tts_prepared:
        esp_lock=json.loads((esp_tts_prepared/'upstream.lock.json').read_text())
        for row in esp_lock['sources']:
            if hashlib.sha256((esp_tts_prepared/'upstream'/row['path']).read_bytes()).hexdigest()!=row['sha256']:
                raise ValueError('Espressif TTS input changed: '+row['path'])
        esp=esp_tts_prepared/'upstream/esp-tts/esp_tts_chinese'
        flags+=['-DGLASS_ESP_TTS','-DGLASS_ESP_TTS_PROBE','-I'+str(esp/'include')]
        voice=output/'esp_voice.S'
        voice.write_text('.section .rodata.esp_voice,"a",@progbits\n.balign 16\n.global glass_esp_voice_data\nglass_esp_voice_data:\n.incbin "'+str(esp/'esp_tts_voice_data_xiaoxin_small.dat')+'"\n.section .note.GNU-stack,"",@progbits\n')
        sources+=[desktop/'glass_esp_tts.c',desktop/'glass_esp_tts_probe.c',voice]
        esp_libraries=[esp/'esp32p4/libesp_tts_chinese.a',esp/'esp32p4/libvoice_set_xiaole.a']
    objects, records = [], []
    for i, source in enumerate(sources):
        obj = output / f'speech-{i}.o'
        extra = ['-O3'] if source.name in compute or source.name in ['snt_nano.c','glass_nano_probe.c','snt_front_q8.c','snt_piperlite_q8.c','glass_zh_q8.c'] else []
        if source.name=='p4tts_bench.c':extra+=['-O3','-fno-fast-math','-ffp-contract=off']
        compiled_source=compute.get(source.name,source)
        cmd = [*cc, *flags, *extra, '-c', str(compiled_source), '-o', str(obj)]
        result = subprocess.run(cmd, cwd=apps/'system/desktop', env=env, capture_output=True, text=True)
        (output/(source.name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:
            print(result.stderr);raise RuntimeError('Speech compile failed: '+source.name)
        objects.append(obj)
        records.append({'source':str(source), 'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                        'compiled_source':str(compiled_source),'compiled_sha256':hashlib.sha256(compiled_source.read_bytes()).hexdigest(),'extra_flags':extra})
        print('SPEECH_RV32_OK', source.name, flush=True)
    archive = output/'libspeech_nuttx.a'
    if archive.exists():archive.unlink()
    subprocess.run([str(toolchain/'riscv32-esp-elf-ar'), 'rcs', str(archive), *map(str,objects)], check=True)
    libraries=output/'lib';libraries.mkdir(exist_ok=True)
    for source in (vendor/'lib').glob('*.a'):
        rename=[arg for name in symbols for arg in ['--redefine-sym',name+'=speech_'+name]]
        subprocess.run([str(toolchain/'riscv32-esp-elf-objcopy'),*rename,str(source),str(libraries/source.name)],check=True)
        if source.name=='libdl_lib.a':
            # Supply the bounded scalar copy in glass_speech_port.c. Other
            # inference kernels remain the pinned vendor implementation.
            subprocess.run([str(toolchain/'riscv32-esp-elf-ar'),'d',str(libraries/source.name),'dl_esp32p4_sr_memcpy.S.obj'],check=True)
    metadata={'sources':records,'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}
    if p4tts_prepared:
        metadata['p4tts_diagnostic']=p4tts_manifest
    if esp_tts_prepared:
        metadata['tts_backend']='esp-tts'
        for library in esp_libraries:
            target=libraries/library.name
            if library.name=='libvoice_set_xiaole.a':
                # Only the pointer-table template changes storage class.
                # The adapter copies it into private RAM before vendor calls.
                subprocess.run([str(toolchain/'riscv32-esp-elf-objcopy'),
                    '--rename-section','.data.template_sylls=.rodata.esp_tts_template_sylls,alloc,load,readonly,data,contents',
                    str(library),str(target)],check=True)
            else:
                subprocess.run([str(toolchain/'riscv32-esp-elf-objcopy'),
                    '--redefine-sym','esp_log_write=speech_tts_log_write',str(library),str(target)],check=True)
        metadata['esp_tts_libraries']=[str(libraries/p.name) for p in esp_libraries]
        metadata['esp_tts_library_sha256']={p.name:hashlib.sha256((libraries/p.name).read_bytes()).hexdigest() for p in esp_libraries}
        metadata['esp_tts_storage_adapter']='Template syllable pointers in flash, copied per call to writable RAM; vendor instructions unchanged'
        metadata['esp_tts_log_adapter']='Private esp_log_write redirect retains WARN/ERROR; suppresses per-character INFO in this archive only'
        metadata['esp_tts_inputs']=esp_lock
    if zh_prepared:
        metadata['zh_fixture_sha256']=hashlib.sha256((zh_prepared/'manifest.json').read_bytes()).hexdigest()
        metadata['zh_model_sha256']={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in (zh_prepared/'model').glob('*.bin')}
    (output/'compile.json').write_text(json.dumps(metadata,indent=2)+'\n')
    return archive


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for field in ['apps','vendor','output','toolchain']:p.add_argument('--'+field,type=Path,required=True)
    p.add_argument('--nano-prepared',type=Path,help='Verified official nano sources, weights and generated fixture (diagnostic only)')
    p.add_argument('--zh-prepared',type=Path,help='Verified custom Chinese Q8 fixture (diagnostic only)')
    p.add_argument('--esp-tts-prepared',type=Path,help='Pinned official ESP-SR TTS P4 libraries and voice data')
    p.add_argument('--p4tts-prepared',type=Path,help='User P4TTS diagnostic tensor export and reference cases; weights loaded from SD')
    a=p.parse_args();build(a.apps.resolve(),a.vendor.resolve(),a.output.resolve(),a.toolchain.resolve(),a.nano_prepared.resolve() if a.nano_prepared else None,a.zh_prepared.resolve() if a.zh_prepared else None,a.esp_tts_prepared.resolve() if a.esp_tts_prepared else None,a.p4tts_prepared.resolve() if a.p4tts_prepared else None)
