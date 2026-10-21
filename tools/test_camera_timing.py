#!/usr/bin/env python3
"""Validate real timing counters and SDK instrumentation idempotence."""
import argparse,os,subprocess,tempfile
from pathlib import Path
from instrument_p4_timing import apply
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];out=a.output.resolve();(out/'nuttx/video').mkdir(parents=True,exist_ok=True)
(out/'nuttx/config.h').write_text('#pragma once\n')
(out/'nuttx/spinlock.h').write_text('''#pragma once
typedef int spinlock_t;typedef int irqstate_t;
#define SP_UNLOCKED 0
static inline irqstate_t spin_lock_irqsave(spinlock_t *p){(void)p;return 0;}
static inline void spin_unlock_irqrestore(spinlock_t *p,irqstate_t f){(void)p;(void)f;}
''')
(out/'esp_attr.h').write_text('#define IRAM_ATTR\n')
(out/'esp_timer.h').write_text('#include <stdint.h>\nuint64_t esp_timer_get_time(void);\n')
overlay=repo/'app/espdl-quickapp/overlay'
(out/'timing.c').write_text('''#include <assert.h>
#include <errno.h>
#include <nuttx/video/camera_timing.h>
static uint64_t current;
uint64_t esp_timer_get_time(void){return current;}
int main(void){
 struct esp_camera_timing s;
 current=10;esp_camera_display_frame();current=16010;esp_camera_display_frame();
 current=66010;esp_camera_display_frame();
 esp_camera_display_guard_frame();
 esp_camera_timing_pan(65000,0);esp_camera_timing_pan(64000,5);
 uint64_t start=esp_camera_flash_begin(CAMERA_FLASH_WRITE);
 esp_camera_timing_read(&s);assert(s.flash_active==1);
 current+=25000;errno=EINTR;esp_camera_flash_end(CAMERA_FLASH_WRITE,4096,start,-5);assert(errno==EINTR);
 esp_camera_timing_read(&s);assert(s.flash_active==0&&s.flash_ops[1]==1&&s.flash_max_us[1]==25000&&s.flash_total_us[1]==25000);
 assert(s.display_frames==3&&s.display_gap_max_us==50000&&s.display_guard_frames==1&&s.pan_errors==1&&s.pan_count==2&&s.pan_max_us==2010);
 return 0;
}
''')
exe=out/'timing'
subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',
 '-I'+str(out),'-I'+str(overlay/'nuttx/include'),str(out/'timing.c'),str(overlay/'nuttx/arch/risc-v/src/esp32p4/esp32p4_camera_timing.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
with tempfile.TemporaryDirectory(prefix='camera-instrument-') as folder:
 root=Path(folder);target=root/'arch/risc-v/src/common/espressif';target.mkdir(parents=True)
 dsi=target/'esp_mipi_dsi.c';flash=target/'esp_spiflash.c'
 dsi.write_text('#include <nuttx/config.h>\nvoid isr(void){\n      g_esp_mipi_dsi_frames++;\n}\n')
 flash.write_text('#include <nuttx/config.h>\n'+''.join('static int spiflash_direct_'+op+'(void){}\n' for op in ['read','write','erase'])+'/* P4 suspends the shared external-memory cache */\n')
 apply(root);first=(dsi.read_text(),flash.read_text());apply(root);assert first==(dsi.read_text(),flash.read_text())
 assert flash.read_text().count('esp_camera_flash_end(')==3
 dsi.write_text('unknown upstream source')
 try:apply(root)
 except RuntimeError:pass
 else:raise AssertionError('Unknown upstream shape was silently patched')
print('CAMERA_TIMING_OK: ISR gap, pan latency/error, Flash active/count/max/total, errno, repeat patch and unknown-source rejection')
