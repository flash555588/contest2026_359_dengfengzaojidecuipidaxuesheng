/* SPDX-License-Identifier: Apache-2.0 */
/* Exact production buffering helpers, deterministic synthesis/playback. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "speech_buffer_config.inc"
struct speech_play {uint64_t started;};
typedef bool (*speech_cancel_fn)(void *);
typedef int (*speech_pcm_fn)(void *,const int16_t *,size_t);
static int mode,allocations,fail_at;
static bool stopped,rendered;
static size_t played;
static int16_t input[40000];
static uint64_t speech_milliseconds(void){return 1000;}
static bool speech_cancelled(void *unused){(void)unused;return stopped;}
static void *buffer_realloc(void *old,size_t size)
{if(++allocations==fail_at)return NULL;return realloc(old,size);}
static int speech_write(void *opaque,const int16_t *pcm,size_t n)
{
  (void)opaque;assert(rendered&&n&&n<=MUSIC_PCM_FRAMES);
  if(stopped)return -ECANCELED;
  assert(!memcmp(pcm,input+played,n*sizeof(*pcm)));played+=n;
  if(mode==2)stopped=true;
  return mode==3?-EIO:0;
}
static int glass_esp_synthesize(const char *text,speech_pcm_fn write,speech_cancel_fn cancel,void *user)
{
  assert(text[0]);size_t offset=0;
  while(offset<40000){
    if(cancel(user))return -ECANCELED;
    size_t n=(offset%2)?591:8192;if(n>40000-offset)n=40000-offset;
    int ret=write(user,input+offset,n);if(ret)return ret;offset+=n;
    if(mode==1)stopped=true;
  }
  rendered=true;return 0;
}
#define realloc buffer_realloc
#include "speech_buffer_production.inc"
#undef realloc
static void run(int kind,int fail,int expected)
{
  mode=kind;fail_at=fail;allocations=0;stopped=rendered=false;played=0;
  struct speech_play play={0};
  assert(speech_esp_play("测试",&play)==expected);
}
int main(void)
{
  for(unsigned i=0;i<40000;i++)input[i]=(int16_t)(i*73+7);
  run(0,0,0);assert(played==40000);
  run(1,0,-ECANCELED);assert(!played);
  run(2,0,-ECANCELED);assert(played==MUSIC_PCM_FRAMES);
  run(3,0,-EIO);assert(played==MUSIC_PCM_FRAMES);
  run(0,1,-ENOMEM);assert(!played);
  run(0,2,-ENOMEM);assert(!played);
  struct speech_buffer buffer={0};stopped=false;
  assert(speech_buffer_write(&buffer,input,16000u*180u+1)==-EFBIG);
  assert(!buffer.data&&!buffer.frames);
  run(0,0,0);assert(played==40000);
  puts("SPEECH_BUFFER_HOST_OK: no playback during rendering; PCM order and bounded playback writes; cancel/render/play/allocation errors; reuse; ASan/UBSan");
}
