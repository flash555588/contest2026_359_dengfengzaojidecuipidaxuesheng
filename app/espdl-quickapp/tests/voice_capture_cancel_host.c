/* SPDX-License-Identifier: Apache-2.0 */
/* Production capture open/close; inert NuttX audio and queue adapters. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdatomic.h>
#define VOICE_BUFFERS 4
#define VOICE_BYTES 1984
#define VOICE_RATE 16000
#define VOICE_SETTLE_FRAMES 1024
#define SPEECH_CAPTURE_TRY 1
static bool cancelled;
#define VOICE_CAPTURE_CANCELLED() cancelled
enum {AUDIOIOC_RESERVE=1,AUDIOIOC_CONFIGURE,AUDIOIOC_REGISTERMQ,AUDIOIOC_GETBUFFERINFO,AUDIOIOC_ALLOCBUFFER,AUDIOIOC_START,AUDIOIOC_ENQUEUEBUFFER,AUDIOIOC_STOP,AUDIOIOC_FREEBUFFER,AUDIOIOC_UNREGISTERMQ,AUDIOIOC_RELEASE,AUDIO_TYPE_INPUT,AUDIO_FMT_PCM};
struct ap_buffer_s {unsigned nbytes,nmaxbytes,curbyte,flags;};
struct audio_caps_desc_s {struct {unsigned ac_len,ac_type,ac_subtype,ac_channels;union {uint16_t hw[4];uint8_t b[8];} ac_controls;} caps;};
struct audio_msg_s {int unused;};
struct ap_buffer_info_s {unsigned nbuffers;};
struct audio_buf_desc_s {unsigned numbytes;union {struct ap_buffer_s **pbuffer,*buffer;} u;};
static unsigned cancel_stage,opened,started,allocated,locked;
static int glass_music_stop_wait(unsigned ms){(void)ms;if(cancel_stage==1)cancelled=true;return 0;}
static int music_pcm_session_trylock(void){locked++;if(cancel_stage==2)cancelled=true;return 0;}
static void music_pcm_session_unlock(void){assert(locked==1);locked--;}
static int fake_open(const char *name,int mode){(void)name;(void)mode;opened++;return 3;}
static int fake_close(int fd){assert(fd==3&&opened==1);opened--;return 0;}
static int fake_ioctl(int fd,unsigned cmd,uintptr_t arg)
{
  assert(fd==3);
  if(cmd==AUDIOIOC_GETBUFFERINFO)((struct ap_buffer_info_s *)arg)->nbuffers=4;
  if(cmd==AUDIOIOC_ALLOCBUFFER){
    struct audio_buf_desc_s *d=(void *)arg;*d->u.pbuffer=calloc(1,sizeof(struct ap_buffer_s));assert(*d->u.pbuffer);
    (*d->u.pbuffer)->nmaxbytes=VOICE_BYTES;allocated++;
    if(cancel_stage==3&&allocated==4)cancelled=true;
    return sizeof(*d);
  }
  if(cmd==AUDIOIOC_START){assert(!cancelled);started++;}
  if(cmd==AUDIOIOC_STOP){assert(started==1);started--;}
  if(cmd==AUDIOIOC_FREEBUFFER){struct audio_buf_desc_s *d=(void *)arg;free(d->u.buffer);allocated--;}
  return 0;
}
static int fake_unlink(const char *name){(void)name;return 0;}
static mqd_t fake_mq_open(const char *name,int mode,int permissions,struct mq_attr *attr)
{(void)name;(void)mode;(void)permissions;(void)attr;return (mqd_t)7;}
static int fake_mq_close(mqd_t mq){assert(mq==(mqd_t)7);return 0;}
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define mq_unlink fake_unlink
#define mq_open fake_mq_open
#define mq_close fake_mq_close
struct voice_ws {bool started,failed,closed;};
static atomic_bool voice_stop;
static int receive_mode,receives;
static int64_t fake_ms;
static int64_t glass_https_milliseconds(void){return fake_ms;}
static int ws_receive(struct voice_ws *ws,int timeout)
{
  fake_ms+=timeout;receives++;
  if(receive_mode==1){atomic_store(&voice_stop,true);ws->started=true;}
  else if(!receive_mode&&receives==2)ws->started=true;
  return 0;
}
#include "voice_capture_production.inc"
int main(void)
{
  for(cancel_stage=0;cancel_stage<=4;cancel_stage++){
    cancelled=cancel_stage==4;struct voice_capture capture;
    int ret=capture_open(&capture);
    assert(ret==(cancel_stage?-ECANCELED:0));
    assert(cancel_stage?!started:started==1);
    capture_close(&capture);assert(!opened&&!started&&!allocated&&!locked);
  }
  struct voice_ws ws={0};assert(!ws_wait_start(&ws)&&ws.started);
  atomic_store(&voice_stop,true);assert(ws_wait_start(&ws)==-ECANCELED);
  atomic_store(&voice_stop,false);ws=(struct voice_ws){0};receive_mode=1;
  assert(ws_wait_start(&ws)==-ECANCELED&&ws.started);
  atomic_store(&voice_stop,false);ws=(struct voice_ws){0};receive_mode=2;
  assert(ws_wait_start(&ws)==-ETIMEDOUT);
  puts("VOICE_CAPTURE_CANCEL_OK: cancellation before capture, after music wait, after ownership lock and before driver start releases every resource");
  puts("VOICE_CONNECT_CANCEL_OK: ready/cancel races reject capture handoff; normal readiness and timeout retained");
  return 0;
}
