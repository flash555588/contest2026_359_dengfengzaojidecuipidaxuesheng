/* SPDX-License-Identifier: Apache-2.0 */
/* Production one-shot wake loop; deterministic model/audio adapters only. */
#include <assert.h>
#include <errno.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glass_speech.h"
#include "glass_custom_wake.h"
#define VOICE_FRAMES 496
#define GLASS_WAKE_PHRASE "你好 openvela"
static pthread_mutex_t speech_lock=PTHREAD_MUTEX_INITIALIZER;
static struct speech_snapshot speech;
static atomic_bool speech_cancel;
static uint64_t speech_milliseconds(void){return 1000;}
static bool model_live,capture_live;
static int create_error,open_error,feed_error,calls,cancel_at;
struct glass_custom_wake {int unused;};
struct voice_capture {int fd;mqd_t mq;};
int glass_custom_wake_create(const char *root,struct glass_custom_wake **out)
{assert(!strcmp(root,"/sdcard/speech"));if(create_error)return create_error;assert(!model_live);*out=calloc(1,sizeof(**out));assert(*out);model_live=true;return 0;}
void glass_custom_wake_destroy(struct glass_custom_wake *wake)
{assert(!capture_live&&model_live);model_live=false;free(wake);}
int glass_custom_wake_feed(struct glass_custom_wake *wake,const int16_t *pcm,size_t frames)
{assert(wake&&pcm&&frames==VOICE_FRAMES&&capture_live&&model_live);return feed_error?feed_error:calls==3;}
unsigned glass_custom_wake_evaluations(const struct glass_custom_wake *wake){assert(wake);return calls;}
float glass_custom_wake_score(const struct glass_custom_wake *wake){assert(wake);return .8f;}
static int capture_open(struct voice_capture *capture)
{(void)capture;if(open_error)return open_error;assert(model_live&&!capture_live);capture_live=true;return 0;}
static void capture_close(struct voice_capture *capture){(void)capture;capture_live=false;}
static int capture_next(struct voice_capture *capture,int16_t *pcm,unsigned *level)
{(void)capture;assert(capture_live);memset(pcm,0,VOICE_FRAMES*sizeof(*pcm));*level=0;calls++;if(calls==cancel_at)atomic_store(&speech_cancel,true);return VOICE_FRAMES;}
static bool speech_cancelled(void *unused){(void)unused;return atomic_load(&speech_cancel);}
/* Wake handoff has no retained TTS weights in this isolated fixture. */
void glass_speech_clear_cache(void){}
#include "wake_handoff_production.inc"
static void reset(void)
{assert(!capture_live&&!model_live);memset(&speech,0,sizeof(speech));atomic_store(&speech_cancel,false);create_error=open_error=feed_error=calls=cancel_at=0;}
int main(void)
{
  reset();speech_publish(SPEECH_WAKE,0,"打开灯光",1,0);assert(speech.command_event==1&&speech.last_command==1&&speech.command_ms==1000);
  speech_publish(SPEECH_COMMAND,0,"等待指令",0,0);assert(speech.command_event==1&&speech.last_command==1);
  reset();assert(!speech_wait_wake());assert(speech.wake_events==1&&speech.frames==3*VOICE_FRAMES&&!model_live&&!capture_live);
  speech_complete(0,true);assert(!speech.active&&speech.wake_ready);
  speech_stop(0);assert(!speech.wake_ready);
  reset();assert(!speech_wait_wake());speech_stop(0);atomic_store(&speech_cancel,true);
  speech_complete(0,true);assert(!speech.wake_ready&&!speech.active&&!speech.error);
  reset();create_error=-ENOENT;assert(speech_wait_wake()==-ENOENT&&!model_live&&!capture_live);
  reset();open_error=-EBUSY;assert(speech_wait_wake()==-EBUSY&&!model_live&&!capture_live);
  reset();feed_error=-EIO;assert(speech_wait_wake()==-EIO&&!model_live&&!capture_live);
  reset();atomic_store(&speech_cancel,true);assert(speech_wait_wake()==-ECANCELED&&!model_live&&!capture_live);
  reset();cancel_at=1;assert(!speech_wait_wake()&&!speech.wake_events&&!model_live&&!capture_live);
  reset();speech.active=true;speech.request=7;speech.phase=SPEECH_WAKE;
  speech_stop(8);assert(!speech_cancelled(NULL)&&speech.phase==SPEECH_WAKE);
  speech_stop(7);assert(speech_cancelled(NULL)&&speech.phase==SPEECH_STOPPING);
  puts("WAKE_HANDOFF_HOST_OK: one-shot release, missing model, busy audio, feed failure, cancellation and stale-owner stop rejection; no MultiNet linked");
  return 0;
}
