#!/usr/bin/env python3
"""Exercise production camera cache ownership and confirmed framebuffer flips."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
out = a.output.resolve()
(out / 'nuttx/video').mkdir(parents=True, exist_ok=True)
(out / 'nuttx/config.h').write_text('#pragma once\n')
(out / 'nuttx/video/fb.h').write_text('''#pragma once
#include <stddef.h>
#define FBIOPAN_DISPLAY 1001
#define FBIOGET_PLANEINFO 1002
struct fb_planeinfo_s { unsigned display, xoffset, yoffset, yres_virtual; void *fbmem; size_t stride, fblen; };
''')
(out / 'esp_cache.h').write_text('''#pragma once
#include <stddef.h>
#define ESP_OK 0
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C (1<<3)
#define ESP_CACHE_MSYNC_FLAG_TYPE_DATA (1<<4)
int esp_cache_msync(void *, size_t, int);
''')
overlay = repo / 'app/espdl-quickapp/overlay'
env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1'}
for line in [64, 128]:
    exe = out / ('camera-sync-' + str(line))
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                    '-DCONFIG_ESPRESSIF_CACHE_L2_CACHE_LINE_SIZE=' + str(line),
                    '-I' + str(out), '-I' + str(overlay / 'apps/system/desktop'),
                    str(repo / 'app/espdl-quickapp/tests/camera_sync_host.c'),
                    str(overlay / 'nuttx/arch/risc-v/src/esp32p4/esp32p4_camera_cache.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, env=env)

exe=out/'camera-handoff'
subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-O1','-g',
 '-fsanitize=address,undefined','-fno-pie','-no-pie',
 '-I'+str(out),'-I'+str(overlay/'apps/system/desktop'),
 str(repo/'app/espdl-quickapp/tests/camera_handoff_host.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,env=env)
