#!/usr/bin/env python3
"""Exercise production ASR sentence assembly and completion policy under ASan."""
import argparse,subprocess,os
from pathlib import Path
from render_all_ui import function
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
text=(desktop/'glass_voice.c').read_text()
structure='struct voice_ws {'+text.split('struct voice_ws {',1)[1].split('\n};',1)[0]+'\n};'
prefix='''#include "glass_voice.h"
#include <cJSON.h>
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VOICE_JSON_BYTES 4096
struct glass_https_request {int unused;};
static void voice_publish(enum glass_voice_phase phase,const char *text,unsigned level)
{(void)phase;(void)text;(void)level;}
static atomic_bool voice_stop;static int64_t fake_ms;static int lock_busy,cancel_after,tries;
static int64_t glass_https_milliseconds(void){return fake_ms;}
static int music_pcm_session_trylock(void){tries++;return lock_busy?-EBUSY:0;}
static void fixture_sleep(unsigned us){fake_ms+=us/1000;if(cancel_after&&tries>=cancel_after)atomic_store(&voice_stop,true);}
#define usleep fixture_sleep
'''
source=prefix+structure+'\n'+'\n'.join(function(text,n) for n in ['append_text','ws_json','transcript_error','voice_audio_lock'])+'\n'+(repo/'app/espdl-quickapp/tests/voice_transcript_host.c').read_text()
test=a.output/'voice-transcript-host.c';test.write_text(source)
cjson=a.apps/'netutils/cjson/cJSON'
binary=a.output/'voice-transcript-host'
subprocess.run(['gcc','-std=gnu11','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(desktop),'-I'+str(cjson),str(test),str(cjson/'cJSON.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
