/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_speech.h"
#include "glass_speech_sr.h"
#include "glass_speech_tts.h"
#include "glass_speech_level.h"
#include "glass_music.h"
#include "glass_music_pcm.h"
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define VOICE_RATE 16000
#define VOICE_BUFFERS 4
#define VOICE_BYTES 4032
#define VOICE_FRAMES (VOICE_BYTES/4)
#define VOICE_SETTLE_FRAMES 1024
#define SPEECH_CAPTURE_TRY 1
#include "glass_voice_capture.inc"

static pthread_mutex_t speech_lock=PTHREAD_MUTEX_INITIALIZER;
static struct speech_snapshot speech;
static uint64_t speech_level_at;
static uint64_t speech_milliseconds(void)
{struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);return (uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;}
static atomic_bool speech_cancel;
static const struct speech_command default_commands[]={
  {1,"打开灯光","da kai deng guang"},{2,"关闭灯光","guan bi deng guang"},
  {3,"打开窗帘","da kai chuang lian"},{4,"关闭窗帘","guan bi chuang lian"},
  {5,"打开风扇","da kai feng shan"},{6,"关闭风扇","guan bi feng shan"},
  {7,"返回桌面","fan hui zhuo mian"},{8,"停止聆听","ting zhi ling ting"}};
/* Recognition is exposed as text/events. HA actions need an explicit mapping
 * to a real entity and are deliberately not inferred from a spoken room name. */
static void speech_publish(enum speech_phase phase,int error,const char *text,int command,unsigned level)
{
  pthread_mutex_lock(&speech_lock);speech.phase=phase;speech.error=error;
  if(text)snprintf(speech.text,sizeof(speech.text),"%s",text);
  speech.command=command;speech.level=level;if(!++speech.revision)speech.revision=1;
  pthread_mutex_unlock(&speech_lock);
}
void glass_speech_get(struct speech_snapshot *out)
{if(out){pthread_mutex_lock(&speech_lock);*out=speech;
  if(out->phase==SPEECH_SPEAKING&&speech_milliseconds()-speech_level_at>180)out->level=0;
  pthread_mutex_unlock(&speech_lock);}}
void glass_speech_stop(void)
{
  pthread_mutex_lock(&speech_lock);
  if(speech.active){atomic_store(&speech_cancel,true);speech.phase=SPEECH_STOPPING;speech.revision++;}
  pthread_mutex_unlock(&speech_lock);
}
static bool speech_cancelled(void *unused)
{(void)unused;return atomic_load(&speech_cancel);}
struct speech_play {struct music_pcm pcm;bool opened;};
static int speech_write(void *opaque,const int16_t *data,size_t frames)
{
  struct speech_play *play=opaque;if(speech_cancelled(NULL))return -ECANCELED;
  if(!play->opened){int ret=music_pcm_try_open(&play->pcm,22050,55);if(ret)return ret;play->opened=true;}
  int ret=music_pcm_write(&play->pcm,data,frames,1);
  if(!ret){unsigned level=glass_speech_pcm_level(data,frames);
    pthread_mutex_lock(&speech_lock);speech.level=level;speech_level_at=speech_milliseconds();pthread_mutex_unlock(&speech_lock);}
  return ret;
}
static int speech_recognize(void)
{
  struct glass_sr *sr=NULL;struct voice_capture capture={.fd=-1,.mq=(mqd_t)-1};
  int ret=glass_sr_create("/sdcard/speech",default_commands,sizeof(default_commands)/sizeof(*default_commands),&sr);
  if(ret)return ret;
  if(speech_cancelled(NULL)){glass_sr_destroy(sr);return -ECANCELED;}
  ret=capture_open(&capture);
  if(!ret){
    speech_publish(SPEECH_WAKE,0,"请说：嗨，乐鑫",0,0);
    while(!speech_cancelled(NULL)){
      int16_t mono[VOICE_FRAMES];unsigned level=0;
      int n=capture_next(&capture,mono,&level);if(n<0){ret=n;break;}if(!n)continue;
      int command=0,event=glass_sr_feed(sr,mono,n,&command);if(event<0){ret=event;break;}
      if(event==3)speech_publish(SPEECH_WAKE,0,"等待唤醒：嗨，乐鑫",0,level);
      if(event==1)speech_publish(SPEECH_COMMAND,0,"请说中文指令",0,level);
      if(event==2){
        for(unsigned i=0;i<sizeof(default_commands)/sizeof(*default_commands);i++)
          if(command==default_commands[i].id){speech_publish(SPEECH_WAKE,0,default_commands[i].text,command,level);break;}
        if(command==8)break;
      }
    }
  }
  capture_close(&capture);glass_sr_destroy(sr);return ret;
}
static void *speech_worker(void *argument)
{
  char *text=argument;int ret;
  if(text){
    struct speech_play play={0};
    speech_publish(SPEECH_SPEAKING,0,"正在合成中文语音",0,0);
    ret=glass_sano_synthesize("/sdcard/speech",text,speech_write,speech_cancelled,&play);
    if(play.opened){if(!ret&&!speech_cancelled(NULL))ret=music_pcm_drain(&play.pcm);music_pcm_close(&play.pcm);}
    free(text);
  }else ret=speech_recognize();
  if(ret==-ECANCELED||speech_cancelled(NULL))ret=0;
  pthread_mutex_lock(&speech_lock);speech.active=false;speech.phase=ret?SPEECH_ERROR:SPEECH_IDLE;
  speech.error=ret;speech.level=0;speech.revision++;pthread_mutex_unlock(&speech_lock);
  return NULL;
}
static int speech_start(const char *text)
{
  char *copy=NULL;if(text){if(!text[0]||strlen(text)>1024)return -EINVAL;copy=strdup(text);if(!copy)return -ENOMEM;}
  pthread_mutex_lock(&speech_lock);
  if(speech.active||music_pcm_session_active()){pthread_mutex_unlock(&speech_lock);free(copy);return -EBUSY;}
  atomic_store(&speech_cancel,false);speech.active=true;speech.phase=SPEECH_LOADING;
  speech.error=speech.command=0;speech.text[0]=0;speech.revision++;
  pthread_attr_t attr;int ret=pthread_attr_init(&attr);
  if(!ret){ret=pthread_attr_setstacksize(&attr,96*1024);
    if(!ret)ret=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if(!ret){pthread_t tid;ret=pthread_create(&tid,&attr,speech_worker,copy);}pthread_attr_destroy(&attr);}
  if(ret){speech.active=false;speech.phase=SPEECH_ERROR;speech.error=-ret;free(copy);}
  pthread_mutex_unlock(&speech_lock);return -ret;
}
int glass_speech_listen(void){return speech_start(NULL);}
int glass_speech_say(const char *text){if(!text)return -EINVAL;return speech_start(text);}
int glass_speech_cli(int argc,char **argv)
{
  if(argc==3&&!strcmp(argv[2],"listen"))return glass_speech_listen();
  if(argc==4&&!strcmp(argv[2],"say"))return glass_speech_say(argv[3]);
  if(argc==3&&!strcmp(argv[2],"stop")){glass_speech_stop();return 0;}
  if(argc==3&&!strcmp(argv[2],"status")){struct speech_snapshot s;glass_speech_get(&s);
    printf("speech active=%u phase=%d error=%d command=%d text=%s\n",s.active,s.phase,s.error,s.command,s.text);return 0;}
  puts("desktop speech listen | say <Chinese text> | stop | status");return -EINVAL;
}
