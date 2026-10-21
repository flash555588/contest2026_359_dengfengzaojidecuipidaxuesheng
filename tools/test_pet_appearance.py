#!/usr/bin/env python3
"""Check production SD appearance reload with real LVGL and ASan/UBSan."""
import argparse
import os
from pathlib import Path
import subprocess
from render_all_ui import function


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apps', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--bundle', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[1]
    desktop = repo / 'app/espdl-quickapp/overlay/apps/system/desktop'
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    source = (desktop / 'pet_lvgl.c').read_text()
    state = source[source.index('enum pet_voice_stage_e'):source.index('static void layout_parts')]
    functions = ['set_blobs_hidden', 'pet_size_index', 'pet_sprite_view',
                 'layout_parts', 'layout_sprite', 'pet_lvgl_is_open',
                 'pet_lvgl_sync', 'pet_lvgl_reload_sprites']
    prelude = '''#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pet_engine.h"
#include "pet_sprites.h"
#include "pet_lvgl.h"
#include "glass_voice.h"
#define BUBBLE_GAP 16
#define INK 0x243040
static const char *appearance_path;
#define PET_SPRITE_PATH appearance_path
'''
    (out / 'production_pet_appearance.inc').write_text(prelude + state + '\n'.join(function(source, n) for n in functions))
    data = bytearray(a.bundle.read_bytes()); data[-1] ^= 1
    invalid = out / 'bad-crc.lvbin'; invalid.write_bytes(data)
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-pthread',
             '-DLV_CONF_INCLUDE_SIMPLE', '-I' + str(desktop), '-I' + str(out),
             '-I' + str(a.apps.resolve() / 'graphics/lvgl'), '-I' + str(a.cache.resolve())]
    objects = sorted(a.cache.resolve().glob('lvgl-*.o'))
    binary = out / 'pet-appearance-host'
    subprocess.run(['gcc', *flags, str(repo / 'app/espdl-quickapp/tests/pet_appearance_host.c'),
                    str(desktop / 'pet_sprites.c'), str(desktop / 'pet_engine.c'),
                    *map(str, objects), '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(a.bundle.resolve()), str(invalid), str(out / 'missing.lvbin')],
                   check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    # Also compile the actual QuickJS API binding against the matching headers.
    subprocess.run(['gcc', *flags, '-I' + str(a.apps.resolve() / 'interpreters/quickjs/quickjs'),
                    '-DCONFIG_BIGNUM', '-c', str(desktop / 'qpk_pet.c'), '-o', str(out / 'qpk_pet.o')], check=True)


if __name__ == '__main__':
    main()
