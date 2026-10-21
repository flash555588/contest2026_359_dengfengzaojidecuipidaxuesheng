#!/usr/bin/env python3
"""Exercise the production guard and the patched prepared DSI ISR on a host."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

from protect_p4_display_flash import apply, restore


def function(source, name, prefix='static int IRAM_ATTR '):
    # The prepared driver also has a forward declaration.
    start = source.rindex(prefix + name + '(')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--nuttx', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[1]
    overlay = repo / 'app/espdl-quickapp/overlay'
    out = a.output.resolve()
    fixture = out / 'prepared'
    common = Path('arch/risc-v/src/common/espressif')
    ll = Path('arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/esp_hal_dma/esp32p4/include/hal/dw_gdma_ll.h')
    paths = [common / 'esp_mipi_dsi.c', common / 'esp_spiflash.c', ll]
    for rel in paths:
        dest = fixture / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(a.nuttx / rel, dest)
    apply(fixture)
    first = {rel: (fixture / rel).read_bytes() for rel in paths}
    apply(fixture)
    assert all((fixture / rel).read_bytes() == data for rel, data in first.items())
    restore(fixture)
    normal={rel:(fixture/rel).read_bytes() for rel in paths}
    restore(fixture)
    assert all((fixture/rel).read_bytes()==data for rel,data in normal.items())
    assert all(b'esp_display_flash_' not in data for data in normal.values())
    apply(fixture)
    assert all((fixture/rel).read_bytes()==data for rel,data in first.items())
    # A partial marker must fail, and failed validation must write no inputs.
    (fixture / ll).write_text('/* ESPCLAW DISPLAY FLASH GUARD */\nunknown upstream')
    before = {rel: (fixture / rel).read_bytes() for rel in paths}
    try:
        apply(fixture)
    except RuntimeError:
        pass
    else:
        raise AssertionError('Incomplete upstream was silently accepted')
    assert all((fixture / rel).read_bytes() == data for rel, data in before.items())
    (fixture / ll).write_bytes(first[ll])
    dsi = first[common / 'esp_mipi_dsi.c'].decode()
    assert 'ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM |' in dsi
    assert 'ret = esp_setup_irq(DW_GDMA_INTR_SOURCE' not in dsi
    (out / 'actual_isr.inc').write_text(function(dsi, 'esp_mipi_dsi_dma_isr') + '\n')
    (out / 'actual_progress.inc').write_text(function(dsi, 'esp_mipi_dsi_flash_progress', 'void ') + '\n')

    (out / 'nuttx').mkdir(exist_ok=True)
    (out / 'hal').mkdir(exist_ok=True)
    (out / 'nuttx/config.h').write_text('#define CONFIG_ESPRESSIF_MIPI_DSI 1\n#define CONFIG_ESPRESSIF_MIPI_CSI 1\n')
    (out / 'nuttx/spinlock.h').write_text('''#pragma once
#include <pthread.h>
#include <stddef.h>
typedef pthread_mutex_t spinlock_t;
typedef int irqstate_t;
#define SP_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static inline irqstate_t spin_lock_irqsave(spinlock_t *p){int e=pthread_mutex_lock(p);if(e)__builtin_trap();return 0;}
static inline void spin_unlock_irqrestore(spinlock_t *p,irqstate_t f){(void)f;int e=pthread_mutex_unlock(p);if(e)__builtin_trap();}
''')
    (out / 'nuttx/arch.h').write_text('#include <stdbool.h>\nvoid up_udelay(unsigned us);\nstatic inline bool up_interrupt_context(void){return false;}\n')
    (out / 'esp_attr.h').write_text('#define IRAM_ATTR\n')
    (out / 'debug.h').write_text('#include <assert.h>\n#define DEBUGASSERT assert\n')
    (out / 'hal/dw_gdma_ll.h').write_text('''#pragma once
#include <stdint.h>
#define DW_GDMA_LL_CHANNELS_PER_GROUP 4
typedef struct {struct {struct {uint32_t val;} int_sig_ena0;uint32_t status;} ch[4];} dw_gdma_dev_t;
''')
    exe = out / 'display-flash'
    subprocess.run([
        'gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-pthread',
        '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-I' + str(out),
        '-I' + str(overlay / 'nuttx/include'),
        str(repo / 'app/espdl-quickapp/tests/display_flash_host.c'),
        str(overlay / 'nuttx/arch/risc-v/src/esp32p4/esp32p4_display_flash.c'),
        '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, env={**os.environ,
        'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    print('DISPLAY_FLASH_OK: production guard, actual DSI ISR, deferred pan/CSI, nested/concurrent Flash, mask lifecycle, errno, patch idempotence/rejection')


if __name__ == '__main__':
    main()
