#!/usr/bin/env python3
"""Exercise the production TTS microphone observer and callback ownership."""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];overlay=repo/'app/espdl-quickapp/overlay'
a.output.mkdir(parents=True,exist_ok=True)
audio=a.output/'nuttx/audio';audio.mkdir(parents=True,exist_ok=True)
(a.output/'nuttx/fs').mkdir(exist_ok=True)
(a.output/'nuttx/fs/ioctl.h').write_text('#pragma once\n#define _AUDIOIOC(n) (0x1000+(n))\n')
(audio/'audio.h').write_text('''#pragma once
#include <stdint.h>
#include <stdatomic.h>
#define AUDIOIOC_STOP 2
struct ap_buffer_s {unsigned nbytes,nmaxbytes,curbyte,flags;atomic_int refs;unsigned char samp[960];};
struct audio_buf_desc_s {unsigned numbytes;union {struct ap_buffer_s **pbuffer;struct ap_buffer_s *buffer;} u;};
int apb_alloc(struct audio_buf_desc_s *);void apb_free(struct ap_buffer_s *);
''')
(audio/'i2s.h').write_text('''#pragma once
#include <nuttx/audio/audio.h>
struct i2s_dev_s {int unused;};
typedef void (*i2s_callback_t)(struct i2s_dev_s *,struct ap_buffer_s *,void *,int);
int fake_receive(struct i2s_dev_s *,struct ap_buffer_s *,i2s_callback_t,void *,unsigned);
int fake_i2s_ioctl(struct i2s_dev_s *,int,unsigned long);
#define I2S_RECEIVE fake_receive
#define I2S_IOCTL fake_i2s_ioctl
''')
exe=a.output/'speech-barge-host'
subprocess.run(['gcc','-std=gnu11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
 '-O1','-g','-pthread','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(a.output),
 '-I'+str(overlay/'nuttx/include'),'-I'+str(overlay/'apps/system/desktop'),
 str(repo/'app/espdl-quickapp/tests/speech_barge_host.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
