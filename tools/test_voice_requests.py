#!/usr/bin/env python3
"""Exercise production latest-wins dispatch, stale owners and PCM framing."""
import argparse,os,subprocess
from pathlib import Path
from render_all_ui import function
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1];desktop=root/'app/espdl-quickapp/overlay/apps/system/desktop'
s=(desktop/'glass_voice.c').read_text()
globals=s[s.index('static pthread_mutex_t voice_lock'):s.index('static int random_bytes')]
stream='struct voice_stream {'+s.split('struct voice_stream {')[1].split('\n};')[0]+'\n};'
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
#include <unistd.h>
#define VOICE_RATE 16000
#define VOICE_PACKET_BYTES 6400
#define VOICE_RING_FRAMES (VOICE_RATE*20u)
struct portal_voice_config {char app_id[33],secret_id[129],secret_key[129];};
static int portal_voice_load(struct portal_voice_config *c){strcpy(c->app_id,"public-fixture");return 0;}
static void voice_run(struct portal_voice_config *config);
'''
names=['voice_changed','voice_publish','voice_finish','glass_voice_get','glass_voice_stop_request','glass_voice_stop','glass_voice_cancel','voice_worker','glass_voice_start_request']
suffix=r'''
static atomic_uint entered;
static atomic_bool release_old,release_new;
static void voice_run(struct portal_voice_config *config)
{
  free(config);unsigned count=atomic_fetch_add(&entered,1)+1;
  if(count==1){
    while(!atomic_load(&release_old))usleep(1000);
    /* Simulate a response already in flight when a newer request arrived. */
    voice_publish(GLASS_VOICE_LISTENING,"OLD PARTIAL",500);
    voice_finish(GLASS_VOICE_ERROR_NONE,"OLD RESULT");
  }else{
    while(!atomic_load(&release_new))usleep(1000);
    voice_publish(GLASS_VOICE_LISTENING,"LATEST",700);
    voice_finish(GLASS_VOICE_ERROR_NONE,"LATEST");
  }
}
static void wait_entered(unsigned n){for(unsigned i=0;i<2000&&atomic_load(&entered)<n;i++)usleep(1000);assert(atomic_load(&entered)==n);}
static void wait_idle(void){for(unsigned i=0;i<2000;i++){pthread_mutex_lock(&voice_lock);bool running=voice_running;pthread_mutex_unlock(&voice_lock);if(!running)return;usleep(1000);}assert(0);}
int main(void)
{
  uint32_t first,second,last;struct glass_voice_snapshot state;
  assert(!glass_voice_start_request(&first));wait_entered(1);
  assert(!glass_voice_start_request(&second));assert(!glass_voice_start_request(&last));
  assert(first!=second&&second!=last&&atomic_load(&voice_cancel));
  atomic_store(&release_old,true);wait_entered(2);
  glass_voice_get(&state);assert(state.request==last&&state.active&&!state.text[0]);
  assert(!atomic_load(&voice_stop));glass_voice_stop_request(first);assert(!atomic_load(&voice_stop));
  glass_voice_stop_request(last);assert(atomic_load(&voice_stop));
  atomic_store(&release_new,true);wait_idle();glass_voice_get(&state);
  assert(!state.active&&state.request==last&&!strcmp(state.text,"LATEST"));
  /* Cancellation by another microphone owner invalidates late completions. */
  atomic_store(&entered,0);atomic_store(&release_old,false);
  assert(!glass_voice_start_request(&first));wait_entered(1);glass_voice_cancel();
  atomic_store(&release_old,true);wait_idle();glass_voice_get(&state);
  assert(!state.active&&state.phase==GLASS_VOICE_IDLE&&!state.text[0]);
  /* PCM packets retain every sample across a wrapped ring and a short tail. */
  struct voice_stream *stream=calloc(1,sizeof(*stream));assert(stream);pthread_mutex_init(&stream->lock,NULL);
  stream->read=VOICE_RING_FRAMES-101;stream->used=6501;stream->done=true;
  for(unsigned i=0;i<6501;i++)stream->pcm[(stream->read+i)%VOICE_RING_FRAMES]=(int16_t)i;
  int16_t packet[VOICE_PACKET_BYTES/2];bool done=false;int error=0;unsigned total=0;
  while(!done){size_t n=voice_stream_take(stream,packet,&done,&error);assert(!error&&n>0&&n<=3200);for(size_t i=0;i<n;i++)assert(packet[i]==(int16_t)(total+i));total+=n;}
  assert(total==6501&&!stream->used);stream->used=100;stream->done=false;
  assert(voice_stream_take(stream,packet,&done,&error)==0&&!done&&stream->used==100);
  pthread_mutex_destroy(&stream->lock);free(stream);
  puts("VOICE_REQUESTS_OK: A/B/C latest wins; teardown serialized; stale results/stops ignored; explicit cancel; ring wrap and short tail lossless");
}
'''
source=prefix+globals+'\n'+stream+'\n'+'\n'.join(function(s,n) for n in names+['voice_stream_take'])+suffix
test=a.output/'voice-requests-host.c';test.write_text(source)
binary=a.output/'voice-requests-host'
subprocess.run(['gcc','-std=gnu11','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I'+str(desktop),str(test),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
