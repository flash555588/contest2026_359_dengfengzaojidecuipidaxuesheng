/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_speech.h"
#include "glass_speech_sr.h"
#include "glass_speech_tts.h"
#ifdef GLASS_ESP_TTS
#include "glass_esp_tts.h"
#endif
#include "glass_speech_level.h"
#include "glass_wake_model.h"
#include "glass_custom_wake.h"
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
#define VOICE_BYTES 1984
#define VOICE_FRAMES (VOICE_BYTES/4)
#define VOICE_SETTLE_FRAMES 1024
#define SPEECH_CAPTURE_TRY 1
static bool speech_cancelled(void *unused);
#define VOICE_CAPTURE_CANCELLED() speech_cancelled(NULL)
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
  if(command>0){speech.last_command=command;speech.command_ms=(uint32_t)speech_milliseconds();if(!++speech.command_event)speech.command_event=1;}
  pthread_mutex_unlock(&speech_lock);
}
void glass_speech_get(struct speech_snapshot *out)
{if(out){pthread_mutex_lock(&speech_lock);*out=speech;
  if(out->phase==SPEECH_SPEAKING&&speech_milliseconds()-speech_level_at>180)out->level=0;
  pthread_mutex_unlock(&speech_lock);}}
static void speech_stop(uint32_t request)
{
  pthread_mutex_lock(&speech_lock);
  if(!request||speech.request==request){
    glass_speech_clear_cache();
    if(speech.wake_ready){speech.wake_ready=false;speech.revision++;}
    if(speech.active){atomic_store(&speech_cancel,true);speech.phase=SPEECH_STOPPING;speech.revision++;}
  }
  pthread_mutex_unlock(&speech_lock);
}
void glass_speech_stop(void){speech_stop(0);}
void glass_speech_clear_cache(void)
{
#ifndef GLASS_ESP_TTS
  glass_sano_cache_clear();
#endif
}
const char *glass_speech_backend(void)
{
#ifdef GLASS_ESP_TTS
  return "esp-tts";
#else
  return "sanotts";
#endif
}
void glass_speech_stop_request(uint32_t request){if(request)speech_stop(request);}
static bool speech_cancelled(void *unused)
{(void)unused;return atomic_load(&speech_cancel);}
struct speech_play {struct music_pcm pcm;bool opened;unsigned rate;uint64_t started,first_write;};
static int speech_write(void *opaque,const int16_t *data,size_t frames)
{
  struct speech_play *play=opaque;if(speech_cancelled(NULL))return -ECANCELED;
  if(!play->opened){int ret=music_pcm_try_open(&play->pcm,play->rate,55);if(ret)return ret;play->opened=true;play->first_write=speech_milliseconds();
    printf("[speech] tts first_pcm_ms=%lu\n",(unsigned long)(speech_milliseconds()-play->started));}
  int ret=music_pcm_write(&play->pcm,data,frames,1);
  if(!ret){unsigned level=glass_speech_pcm_level(data,frames);
    pthread_mutex_lock(&speech_lock);speech.level=level;speech_level_at=speech_milliseconds();pthread_mutex_unlock(&speech_lock);}
  return ret;
}
#ifdef GLASS_ESP_TTS
struct speech_buffer {int16_t *data;size_t frames,capacity;};
static int speech_buffer_write(void *opaque,const int16_t *data,size_t frames)
{
  struct speech_buffer *buffer=opaque;
  const size_t limit=16000u*180u;
  if(speech_cancelled(NULL))return -ECANCELED;
  if(frames>limit-buffer->frames)return -EFBIG;
  size_t needed=buffer->frames+frames;
  if(needed>buffer->capacity){
    size_t capacity=buffer->capacity?buffer->capacity*2:16000;
    if(capacity<needed)capacity=needed;
    if(capacity>limit)capacity=limit;
    int16_t *next=realloc(buffer->data,capacity*sizeof(*next));
    if(!next)return -ENOMEM;
    buffer->data=next;buffer->capacity=capacity;
  }
  memcpy(buffer->data+buffer->frames,data,frames*sizeof(*data));
  buffer->frames=needed;return 0;
}
static int speech_esp_play(const char *text,struct speech_play *play)
{
  struct speech_buffer buffer={0};
  int ret=glass_esp_synthesize(text,speech_buffer_write,speech_cancelled,&buffer);
  /* The vendor can produce uneven chunks. Finish this bounded UI phrase
   * before opening DMA, so playback never waits for parser/decoder work. */
  printf("[speech] prebuffer frames=%u render_ms=%lu result=%d\n",(unsigned)buffer.frames,
    (unsigned long)(speech_milliseconds()-play->started),ret);
  for(size_t offset=0;!ret&&offset<buffer.frames;){
    size_t count=buffer.frames-offset;
    if(count>MUSIC_PCM_FRAMES)count=MUSIC_PCM_FRAMES;
    ret=speech_write(play,buffer.data+offset,count);
    offset+=count;
  }
  free(buffer.data);return ret;
}
#endif
static int speech_wait_wake(void)
{
  struct glass_custom_wake *wake=NULL;
  struct voice_capture capture={.fd=-1,.mq=(mqd_t)-1};
  int ret=glass_custom_wake_create("/sdcard/speech",&wake);
  if(ret)return ret;
  if(speech_cancelled(NULL)){glass_custom_wake_destroy(wake);return -ECANCELED;}
  ret=capture_open(&capture);
  if(!ret){
    speech_publish(SPEECH_WAKE,0,"请说：" GLASS_WAKE_PHRASE,0,0);
    while(!speech_cancelled(NULL)){
      int16_t mono[VOICE_FRAMES];unsigned level=0;
      int n=capture_next(&capture,mono,&level);if(n<0){ret=n;break;}if(!n)continue;
      int event=glass_custom_wake_feed(wake,mono,n);if(event<0){ret=event;break;}
      pthread_mutex_lock(&speech_lock);
      speech.frames+=n;speech.level=level;
      speech.wake_evaluations=glass_custom_wake_evaluations(wake);
      speech.wake_score=glass_custom_wake_score(wake);
      if(event>0)speech.wake_events++;
      pthread_mutex_unlock(&speech_lock);
      if(event>0)break;
    }
  }
  /* The UI may open cloud ASR only after worker completion is published. */
  capture_close(&capture);glass_custom_wake_destroy(wake);return ret;
}
static int speech_recognize(void)
{
  struct glass_sr *sr=NULL;struct voice_capture capture={.fd=-1,.mq=(mqd_t)-1};
  int ret=glass_sr_create("/sdcard/speech",default_commands,sizeof(default_commands)/sizeof(*default_commands),&sr);
  if(ret)return ret;
  if(speech_cancelled(NULL)){glass_sr_destroy(sr);return -ECANCELED;}
  ret=capture_open(&capture);
  if(!ret){
    speech_publish(SPEECH_WAKE,0,"请说：" GLASS_WAKE_PHRASE,0,0);
    while(!speech_cancelled(NULL)){
      int16_t mono[VOICE_FRAMES];unsigned level=0;
      int n=capture_next(&capture,mono,&level);if(n<0){ret=n;break;}if(!n)continue;
      int command=0,event=glass_sr_feed(sr,mono,n,&command);if(event<0){ret=event;break;}
      unsigned evaluations=0;float score=0;glass_sr_metrics(sr,&evaluations,&score);
      pthread_mutex_lock(&speech_lock);speech.wake_evaluations=evaluations;speech.wake_score=score;
      speech.frames+=n;if(event==1)speech.wake_events++;pthread_mutex_unlock(&speech_lock);
      if(event==3)speech_publish(SPEECH_WAKE,0,"等待唤醒：" GLASS_WAKE_PHRASE,0,level);
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
static int speech_probe(void)
{
  const char *names[]={"wake-positive.pcm","wake-negative.pcm"};
  for(unsigned i=0;i<2;i++){
    struct glass_sr *sr=NULL;int ret=glass_sr_create("/sdcard/speech",default_commands,sizeof(default_commands)/sizeof(*default_commands),&sr);
    if(ret)return ret;
    char path[128];snprintf(path,sizeof(path),"/sdcard/speech/test/%s",names[i]);FILE *file=fopen(path,"rb");
    if(!file){ret=-errno;glass_sr_destroy(sr);return ret;}
    unsigned events=0,total=0;int16_t pcm[1024];size_t n;
    uint64_t started=speech_milliseconds();
    while((n=fread(pcm,sizeof(int16_t),1024,file))>0){
      if(speech_cancelled(NULL)){ret=-ECANCELED;break;}
      total+=n;if(total>16000*30){ret=-EFBIG;break;}
      int command=0,event=glass_sr_feed(sr,pcm,n,&command);if(event<0){ret=event;break;}if(event==1)events++;
    }
    if(ferror(file)&&!ret)ret=-EIO;fclose(file);
    unsigned evaluations;float score;glass_sr_metrics(sr,&evaluations,&score);
    printf("[speech] fixture=%s events=%u evaluations=%u score=%.4f samples=%u compute_ms=%lu\n",
      names[i],events,evaluations,(double)score,total,(unsigned long)(speech_milliseconds()-started));
    if(!ret&&events!=(i==0?1:0))ret=-EBADMSG;
    glass_sr_destroy(sr);if(ret)return ret;
  }
  speech_publish(SPEECH_IDLE,0,"唤醒模型离线测试通过（非真人测试）",0,0);return 0;
}
#ifdef GLASS_NANO_PROBE
#include "glass_nano_probe.h"
#endif
#ifdef GLASS_ZH_Q8_PROBE
#include "glass_zh_q8.h"
#endif
#ifdef GLASS_ESP_TTS_PROBE
#include "glass_esp_tts_probe.h"
#endif
struct speech_job {char *text;bool probe,wake_only;int nano;};
static void speech_complete(int ret,bool wake_only)
{
  pthread_mutex_lock(&speech_lock);
  bool cancelled=speech_cancelled(NULL);
  if(ret==-ECANCELED||cancelled)ret=0;
  speech.wake_ready=wake_only&&!ret&&!cancelled&&speech.wake_events>0;
  speech.active=false;speech.phase=ret?SPEECH_ERROR:SPEECH_IDLE;
  speech.error=ret;speech.level=0;speech.revision++;
  pthread_mutex_unlock(&speech_lock);
}
static void *speech_worker(void *argument)
{
#if defined(__riscv) && defined(CONFIG_ARCH_CHIP_ESP32P4)
#if !CONFIG_ARCH_RISCV_INTXCPT_EXTENSIONS
#error "P4 speech kernels require saved PIE and hardware-loop context"
#endif
  /* NuttX initializes both per-task coprocessors disabled. Enable them in
   * this worker, as the ESP-DL worker does; the extended-context hook keeps
   * their registers isolated across preemption and CPU migration. */
  __asm__ volatile("csrwi 0x7f2, 1\ncsrwi 0x7f1, 1" ::: "memory");
#endif
  struct speech_job *job=argument;char *text=job->text;bool probe=job->probe,wake_only=job->wake_only;int nano=job->nano;free(job);int ret;
  (void)nano;
#ifdef GLASS_P4TTS_PROBE
  if(nano>=30){
    extern int glass_p4tts_probe(int,bool (*)(void *),void *);
    speech_publish(SPEECH_SPEAKING,0,"User P4TTS performance diagnostic",0,0);
    ret=glass_p4tts_probe(nano-30,speech_cancelled,NULL);
  }
  else
#endif
#ifdef GLASS_ESP_TTS_PROBE
  if(nano>=20){speech_publish(SPEECH_SPEAKING,0,"Espressif Chinese TTS diagnostic",0,0);ret=glass_esp_tts_probe(nano-20,speech_cancelled,NULL);}
  else
#endif
#ifdef GLASS_ZH_Q8_PROBE
  if(nano>=10){speech_publish(SPEECH_SPEAKING,0,"Custom Chinese Q8 diagnostic",0,0);ret=glass_zh_q8_probe(nano-10,speech_cancelled,NULL);}
  else
#endif
#ifdef GLASS_NANO_PROBE
  if(nano){speech_publish(SPEECH_SPEAKING,0,"Official English INT8 diagnostic",0,0);ret=glass_nano_probe(nano-1,speech_cancelled,NULL);}
  else
#endif
  if(probe)ret=speech_probe();
  else if(wake_only)ret=speech_wait_wake();
  else if(text){
    uint64_t started=speech_milliseconds();
    struct speech_play play={.started=started,
#ifdef GLASS_ESP_TTS
      .rate=GLASS_ESP_TTS_RATE
#else
      .rate=22050
#endif
    };uint64_t played=0;
    speech_publish(SPEECH_SPEAKING,0,"正在合成中文语音",0,0);
#ifdef GLASS_ESP_TTS
    ret=speech_esp_play(text,&play);
#else
    ret=glass_sano_synthesize("/sdcard/speech",text,speech_write,speech_cancelled,&play);
#endif
    if(play.opened){
      if(!ret&&!speech_cancelled(NULL))ret=music_pcm_drain(&play.pcm);
      played=play.pcm.played_samples;
      uint64_t elapsed=speech_milliseconds()-play.first_write;
      printf("[speech] playback frames=%llu expected_ms=%llu elapsed_ms=%llu result=%d\n",
        (unsigned long long)played,(unsigned long long)(played*1000/play.rate),(unsigned long long)elapsed,ret);
      music_pcm_close(&play.pcm);
    }
    if(ret||speech_cancelled(NULL))glass_speech_clear_cache();
    printf("[speech] tts done result=%d elapsed_ms=%lu played_frames=%llu\n",ret,(unsigned long)(speech_milliseconds()-started),(unsigned long long)played);
    free(text);
  }else ret=speech_recognize();
  speech_complete(ret,wake_only);
  return NULL;
}
static int speech_start_mode(const char *text,bool probe,bool wake_only,uint32_t *request,int nano)
{
  char *copy=NULL;if(text){if(!text[0]||strlen(text)>1024)return -EINVAL;copy=strdup(text);if(!copy)return -ENOMEM;}
  struct speech_job *job=calloc(1,sizeof(*job));if(!job){free(copy);return -ENOMEM;}job->text=copy;job->probe=probe;job->wake_only=wake_only;job->nano=nano;
  pthread_mutex_lock(&speech_lock);
  if(speech.active||music_pcm_session_active()){pthread_mutex_unlock(&speech_lock);free(copy);free(job);return -EBUSY;}
  if(!text)glass_speech_clear_cache();
  atomic_store(&speech_cancel,false);speech.active=true;speech.phase=SPEECH_LOADING;
  speech.error=speech.command=0;speech.text[0]=0;speech.revision++;
  speech.wake_ready=false;
  if(!++speech.request)speech.request=1;
  speech.frames=speech.wake_events=speech.wake_evaluations=0;speech.wake_score=0;
  speech.command_event=speech.command_ms=0;speech.last_command=0;
  pthread_attr_t attr;int ret=pthread_attr_init(&attr);
  if(!ret){ret=pthread_attr_setstacksize(&attr,96*1024);
    if(!ret)ret=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if(!ret){pthread_t tid;ret=pthread_create(&tid,&attr,speech_worker,job);}pthread_attr_destroy(&attr);}
  if(ret){speech.active=false;speech.phase=SPEECH_ERROR;speech.error=-ret;free(copy);free(job);}
  else if(request)*request=speech.request;
  pthread_mutex_unlock(&speech_lock);return -ret;
}
static int speech_start(const char *text,bool probe,bool wake_only,uint32_t *request)
{return speech_start_mode(text,probe,wake_only,request,0);}
int glass_speech_listen(void){return speech_start(NULL,false,false,NULL);}
int glass_speech_wait_wake(uint32_t *request){if(!request)return -EINVAL;*request=0;return speech_start(NULL,false,true,request);}
int glass_speech_say(const char *text){if(!text)return -EINVAL;return speech_start(text,false,false,NULL);}
int glass_speech_probe(void){return speech_start(NULL,true,false,NULL);}
int glass_speech_nano(int mode)
{
#ifdef GLASS_NANO_PROBE
  if(mode<0||mode>2)return -EINVAL;
  return speech_start_mode(NULL,false,false,NULL,mode+1);
#else
  (void)mode;return -ENOSYS;
#endif
}
int glass_speech_zh_q8(int mode)
{
#ifdef GLASS_ZH_Q8_PROBE
  if(mode<0||mode>5)return -EINVAL;
  return speech_start_mode(NULL,false,false,NULL,mode+10);
#else
  (void)mode;return -ENOSYS;
#endif
}
int glass_speech_esp_tts(int mode)
{
#ifdef GLASS_ESP_TTS_PROBE
  if(mode<0||mode>2)return -EINVAL;
  return speech_start_mode(NULL,false,false,NULL,mode+20);
#else
  (void)mode;return -ENOSYS;
#endif
}
int glass_speech_p4tts(int mode)
{
#ifdef GLASS_P4TTS_PROBE
  if(mode<0||mode>2)return -EINVAL;
  return speech_start_mode(NULL,false,false,NULL,mode+30);
#else
  (void)mode;return -ENOSYS;
#endif
}
int glass_speech_cli(int argc,char **argv)
{
#ifdef GLASS_ESP_TTS
  if((argc==3||argc==4)&&!strcmp(argv[2],"speed")){
    if(argc==4){
      if(argv[3][0]<'0'||argv[3][0]>'5'||argv[3][1])return -EINVAL;
      int ret=glass_esp_tts_set_speed((unsigned)(argv[3][0]-'0'));if(ret)return ret;
    }
    printf("speech backend=esp-tts speed=%u range=0..5\n",glass_esp_tts_speed());return 0;
  }
#endif
  if(argc==3&&!strcmp(argv[2],"listen"))return glass_speech_listen();
  if(argc==4&&!strcmp(argv[2],"say"))return glass_speech_say(argv[3]);
  if(argc==3&&!strcmp(argv[2],"stop")){glass_speech_stop();return 0;}
  if(argc==3&&!strcmp(argv[2],"status")){struct speech_snapshot s;glass_speech_get(&s);
    printf("speech active=%u phase=%d error=%d command=%d wakes=%u evaluations=%u score=%.4f frames=%u text=%s\n",
      s.active,s.phase,s.error,s.command,s.wake_events,s.wake_evaluations,(double)s.wake_score,s.frames,s.text);return 0;}
  puts("desktop speech listen | say <Chinese text> | stop | status");return -EINVAL;
}
