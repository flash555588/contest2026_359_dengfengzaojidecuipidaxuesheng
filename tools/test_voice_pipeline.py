#!/usr/bin/env python3
"""Run actual capture/transport workers with a slow network and ordered PCM."""
import argparse,os,subprocess
from pathlib import Path
from render_all_ui import function
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1];desktop=root/'app/espdl-quickapp/overlay/apps/system/desktop';s=(desktop/'glass_voice.c').read_text()
structs='\n'.join('struct '+name+' {'+s.split('struct '+name+' {')[1].split('\n};')[0]+'\n};' for name in ['voice_ws','voice_stream'])
prefix=r'''
#include "glass_voice.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define VOICE_RATE 16000
#define VOICE_FRAMES 240
#define VOICE_PACKET_BYTES 6400
#define VOICE_JSON_BYTES 4096
#define VOICE_RING_FRAMES (VOICE_RATE*20u)
struct glass_https_request {int unused;};
struct portal_voice_config {int unused;};
struct voice_capture {int unused;};
static pthread_mutex_t voice_lock=PTHREAD_MUTEX_INITIALIZER;
static struct glass_voice_snapshot voice;
static atomic_bool voice_stop,voice_cancel;
static uint32_t voice_worker_request=1;
static unsigned produced,received,packets;
static int64_t glass_https_milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int voice_path(struct portal_voice_config *c,char *p,size_t n){(void)c;snprintf(p,n,"fixture");return 0;}
static int capture_open(struct voice_capture *c){(void)c;return 0;}
static void capture_close(struct voice_capture *c){(void)c;}
static int capture_next(struct voice_capture *c,int16_t *pcm,unsigned *level){(void)c;usleep(15000);if(produced>=VOICE_FRAMES*140){atomic_store(&voice_stop,true);return 0;}for(unsigned i=0;i<VOICE_FRAMES;i++)pcm[i]=(int16_t)(produced+i);produced+=VOICE_FRAMES;*level=1000;return VOICE_FRAMES;}
static void glass_https_set_deadline(void *c,int64_t d){(void)c;(void)d;}
'''
fakes=r'''
static int ws_open(struct voice_ws *ws,const char *path){(void)path;memset(ws,0,sizeof(*ws));usleep(1000000);ws->started=true;return 0;}
static int ws_wait_start(struct voice_ws *ws){return ws->started?0:-EIO;}
static void ws_close(struct voice_ws *ws){(void)ws;}
static int ws_receive(struct voice_ws *ws,int timeout){(void)ws;(void)timeout;return 0;}
static int ws_send_frame(struct voice_ws *ws,unsigned opcode,const void *data,size_t bytes){
 if(opcode==2){assert(bytes<=VOICE_PACKET_BYTES&&!(bytes%2));const int16_t *pcm=data;for(size_t i=0;i<bytes/2;i++)assert(pcm[i]==(int16_t)(received+i));received+=bytes/2;packets++;usleep(70000);}
 else if(opcode==1){assert(received==produced);strcpy(ws->final_text,"fixture transcript");ws->finished=true;}
 return 0;
}
'''
names=['voice_changed','voice_publish','voice_finish','connect_error','transcript_error','voice_capture_worker','voice_stream_take','voice_run']
suffix=r'''
int main(void){voice.request=1;voice.active=true;voice.phase=GLASS_VOICE_CONNECTING;int64_t start=glass_https_milliseconds();voice_run(calloc(1,sizeof(struct portal_voice_config)));assert(!voice.active&&voice.phase==GLASS_VOICE_DONE&&!voice.error);assert(produced==VOICE_FRAMES*140&&received==produced&&packets==11);assert(glass_https_milliseconds()-start<6000);puts("VOICE_PIPELINE_OK: real producer and transport functions; 1 s connection + 70 ms packet writes; 33600 ordered samples retained, 11 packets, no lost beginning/tail");}
'''
source=prefix+structs+fakes+'\n'.join(function(s,n) for n in names)+suffix
test=a.output/'voice-pipeline-host.c';test.write_text(source);binary=a.output/'voice-pipeline-host'
subprocess.run(['gcc','-std=gnu11','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I'+str(desktop),str(test),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
