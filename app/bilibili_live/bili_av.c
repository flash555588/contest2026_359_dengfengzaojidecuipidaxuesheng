/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_av.h"
#include "bili_catchup.h"
#include "aacdec.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#define VIDEO_QUEUE 6
#define AUDIO_QUEUE 64
#define VIDEO_BYTES (2u*1024u*1024u)
#define AUDIO_LIMIT 16384u
struct packet {uint8_t *data;size_t size;uint32_t ts;unsigned generation;};
struct queue {struct packet packets[AUDIO_QUEUE];unsigned head,count;size_t bytes;};
struct bili_av {
  pthread_mutex_t lock;
  pthread_cond_t ready;
  pthread_t video_thread,audio_thread;
  bool video_started,audio_started,stopping,seeking,clock_valid,audio_ready;
  atomic_bool *cancel;
  int error;
  struct bili_av_ops ops;void *opaque;
  struct queue video,audio;
  uint8_t config[BILI_CONFIG_LIMIT];size_t config_size;
  uint8_t asc[64];size_t asc_size;unsigned audio_generation;
  unsigned generation,active_generation;
  uint32_t origin_ts,latest_ts,audio_ts;
  uint64_t origin_us,audio_us,last_render_us;
  struct bili_av_stats stats;
};
static uint64_t now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static bool stopped(struct bili_av *s){return s->stopping||atomic_load(s->cancel)||s->error;}
static void clear(struct queue *q){while(q->count){free(q->packets[q->head].data);q->head=(q->head+1)%AUDIO_QUEUE;q->count--;}q->bytes=0;}
static void discard_head(struct queue *q){struct packet *p=&q->packets[q->head];q->bytes-=p->size;free(p->data);q->head=(q->head+1)%AUDIO_QUEUE;q->count--;}
/* Preserve the next dependent picture so it can release the decoder's delayed
 * output even if a slow IDR was still decoding when the queue filled. */
static void keep_next(struct queue *q,unsigned generation){
  if(!q->count)return;
  struct packet next=q->packets[q->head];
  q->head=(q->head+1)%AUDIO_QUEUE;q->count--;q->bytes-=next.size;
  clear(q);q->packets[q->head]=next;
  q->packets[q->head].generation=generation;q->count=1;q->bytes=next.size;
}
static void fail(struct bili_av *s,int error){pthread_mutex_lock(&s->lock);if(!s->error)s->error=error;pthread_cond_broadcast(&s->ready);pthread_mutex_unlock(&s->lock);}
static void anchor(struct bili_av *s,uint32_t ts){if(!s->clock_valid){s->clock_valid=true;s->origin_ts=ts;s->origin_us=now_us();}}
static int32_t lag(struct bili_av *s,uint32_t ts){
  uint64_t now=now_us();
  uint32_t clock=s->audio_ready&&now-s->audio_us<500000?s->audio_ts+(uint32_t)((now-s->audio_us)/1000):s->origin_ts+(uint32_t)((now-s->origin_us)/1000);
  return s->clock_valid?(int32_t)(clock-ts):0;
}
static int push(struct queue *q,const uint8_t *p,size_t n,uint32_t ts,unsigned gen,const uint8_t *prefix,size_t prefix_size){
  struct packet packet={.data=calloc(1,prefix_size+n+64),.size=prefix_size+n,.ts=ts,.generation=gen};
  if(!packet.data)return -ENOMEM;
  if(prefix_size)memcpy(packet.data,prefix,prefix_size);
  memcpy(packet.data+prefix_size,p,n);
  q->packets[(q->head+q->count)%AUDIO_QUEUE]=packet;q->count++;q->bytes+=packet.size;return 0;
}
static bool pop(struct bili_av *s,struct queue *q,struct packet *p){
  pthread_mutex_lock(&s->lock);
  while(!stopped(s)&&!q->count){struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=100000000;if(t.tv_nsec>=1000000000){t.tv_nsec-=1000000000;t.tv_sec++;}pthread_cond_timedwait(&s->ready,&s->lock,&t);}
  if(stopped(s)){pthread_mutex_unlock(&s->lock);return false;}
  *p=q->packets[q->head];q->head=(q->head+1)%AUDIO_QUEUE;q->count--;q->bytes-=p->size;
  pthread_mutex_unlock(&s->lock);return true;
}
static int rendered(void *opaque,const uint8_t *const planes[3],const int stride[2],int w,int h,uint64_t pts){
  struct bili_av *s=opaque;pthread_mutex_lock(&s->lock);s->stats.decoded++;
  int32_t behind=lag(s,(uint32_t)pts);s->stats.lag=behind>0?behind:0;
  bool skip=stopped(s)||(behind>150&&now_us()-s->last_render_us<150000);
  if(skip)s->stats.render_skip++;
  pthread_mutex_unlock(&s->lock);
  if(skip)return 0;
  /* Wait only for early video. Audio continues on its own worker. */
  uint64_t wait_until=now_us()+50000;
  while(behind < -30&&now_us()<wait_until){usleep(5000);pthread_mutex_lock(&s->lock);skip=stopped(s);behind=lag(s,(uint32_t)pts);pthread_mutex_unlock(&s->lock);if(skip)return 0;}
  int ret=s->ops.frame(s->opaque,planes,stride,w,h,pts);
  pthread_mutex_lock(&s->lock);s->last_render_us=now_us();if(!ret)s->stats.displayed++;pthread_mutex_unlock(&s->lock);return ret;
}
static void *video_worker(void *opaque){
  struct bili_av *s=opaque;void *decoder=bili_decoder_create_timed(rendered,s);
  if(!decoder){fail(s,-ENOMEM);return NULL;}
  struct packet p;
  while(pop(s,&s->video,&p)){
    pthread_mutex_lock(&s->lock);s->active_generation=p.generation;bool skip=p.generation!=s->generation;pthread_mutex_unlock(&s->lock);
    if(!skip){uint64_t start=now_us();int ret=bili_decoder_feed_timestamp(decoder,p.data,p.size,p.ts);pthread_mutex_lock(&s->lock);s->stats.decode_us+=now_us()-start;pthread_mutex_unlock(&s->lock);if(ret){free(p.data);fail(s,ret);break;}}
    free(p.data);
  }
  bili_decoder_destroy(decoder);return NULL;
}
static int audio_config(const uint8_t *p,size_t n,AACFrameInfo *info){
  static const unsigned rates[]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
  if(n<2)return -EPROTO;
  unsigned object=p[0]>>3,index=((p[0]&7)<<1)|(p[1]>>7),channels=(p[1]>>3)&15;
  if(object!=2||index>=13||rates[index]<8000||rates[index]>48000||channels<1||channels>2||(p[1]&7))return -ENOTSUP;
  memset(info,0,sizeof(*info));info->nChans=channels;info->sampRateCore=info->sampRateOut=rates[index];info->profile=1;return 0;
}
static void *audio_worker(void *opaque){
  struct bili_av *s=opaque;HAACDecoder decoder=NULL;int16_t *pcm=malloc(4096*sizeof(int16_t));bool opened=false;unsigned gen=0;uint64_t submitted=0;int ret=0;
  if(!pcm){fail(s,-ENOMEM);return NULL;}
  struct packet p;
  while(pop(s,&s->audio,&p)){
    uint8_t config[64];size_t config_size;pthread_mutex_lock(&s->lock);config_size=s->asc_size;memcpy(config,s->asc,config_size);bool obsolete=p.generation!=s->audio_generation;pthread_mutex_unlock(&s->lock);
    if(obsolete){free(p.data);continue;}
    AACFrameInfo info;
    if(gen!=p.generation){
      if(opened){s->ops.audio_close(s->opaque);opened=false;}
      if(decoder)AACFreeDecoder(decoder);
      decoder=AACInitDecoder();ret=decoder?audio_config(config,config_size,&info):-ENOMEM;
      if(!ret&&AACSetRawBlockParams(decoder,0,&info))ret=-ENOTSUP;
      if(!ret){ret=s->ops.audio_open(s->opaque,info.sampRateCore);opened=!ret;}
      if(ret){free(p.data);break;}gen=p.generation;submitted=0;
    }
    unsigned char *input=p.data;int left=p.size;
    ret=AACDecode(decoder,&input,&left,pcm)?-EPROTO:0;
    if(!ret){AACGetLastFrameInfo(decoder,&info);if(info.nChans<1||info.nChans>2||info.outputSamps<1||info.outputSamps>4096||info.sampRateOut<8000||info.sampRateOut>48000)ret=-ENOTSUP;}
    if(!ret){unsigned frames=info.outputSamps/info.nChans;ret=s->ops.audio_write(s->opaque,pcm,frames,info.nChans);submitted+=frames;
      if(!ret){unsigned played=s->ops.audio_time(s->opaque);uint64_t submitted_ms=submitted*1000/info.sampRateOut;unsigned pending=submitted_ms>played?submitted_ms-played:0;
        pthread_mutex_lock(&s->lock);s->audio_ts=p.ts+frames*1000/info.sampRateOut-pending;s->audio_us=now_us();s->audio_ready=true;s->stats.audio_frames++;pthread_mutex_unlock(&s->lock);}}
    free(p.data);if(ret)break;
  }
  if(opened)s->ops.audio_close(s->opaque);
  if(decoder)AACFreeDecoder(decoder);
  free(pcm);
  if(ret)fail(s,ret);
  return NULL;
}
struct bili_av *bili_av_create(const struct bili_av_ops *ops,void *opaque,atomic_bool *cancel){
  if(!ops||!ops->frame||!ops->audio_open||!ops->audio_write||!ops->audio_time||!ops->audio_close||!cancel)return NULL;
  struct bili_av *s=calloc(1,sizeof(*s));if(!s)return NULL;s->ops=*ops;s->opaque=opaque;s->cancel=cancel;s->seeking=true;
  pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->ready,NULL);pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,65536);
  int ret=pthread_create(&s->video_thread,&attr,video_worker,s);s->video_started=!ret;
  if(!ret){ret=pthread_create(&s->audio_thread,&attr,audio_worker,s);s->audio_started=!ret;}
  pthread_attr_destroy(&attr);if(ret){bili_av_destroy(s);return NULL;}return s;
}
int bili_av_video(struct bili_av *s,const uint8_t *p,size_t n,uint32_t ts){
  uint8_t config[BILI_CONFIG_LIMIT];struct bili_access_unit au;int ret=bili_inspect_au(p,n,&au,config);if(ret)return ret;
  pthread_mutex_lock(&s->lock);if(stopped(s)){ret=s->error?s->error:-ECANCELED;goto done;}
  if(au.sps||au.pps){if(au.sps&&au.pps){memcpy(s->config,config,au.config_bytes);s->config_size=au.config_bytes;}else s->config_size=0;}
  if(!au.vcl)goto done;
  anchor(s,ts);
  /* Detect actual timestamp restarts, not small B-frame composition offsets. */
  if((int32_t)(ts-s->latest_ts)<-1000){s->origin_ts=ts;s->origin_us=now_us();s->audio_ready=false;s->seeking=true;s->generation++;s->stats.decode_skip+=s->video.count;clear(&s->video);}
  s->latest_ts=ts;
  if(au.disposable&&lag(s,ts)>150){s->stats.decode_skip++;goto done;}
  if(s->video.count>=VIDEO_QUEUE||s->video.bytes+n+s->config_size>VIDEO_BYTES){s->stats.decode_skip+=s->video.count?s->video.count-1:0;s->generation++;keep_next(&s->video,s->generation);s->seeking=true;s->stats.resync++;}
  if(s->seeking&&!au.idr){s->stats.decode_skip++;goto done;}
  if(au.idr&&!s->config_size){ret=-EPROTO;goto done;}
  /* A live source itself can be behind wall time. Never reject every IDR on
   * that basis: the first accepted keyframe is what allows playback to recover. */
  if(s->seeking&&au.idr){s->origin_ts=ts;s->origin_us=now_us();}
  if(s->video.bytes+n+(au.idr?s->config_size:0)>VIDEO_BYTES){s->stats.decode_skip+=s->video.count;clear(&s->video);}
  ret=push(&s->video,p,n,ts,s->generation,au.idr?s->config:NULL,au.idr?s->config_size:0);
  if(!ret){s->seeking=false;pthread_cond_broadcast(&s->ready);}
done:pthread_mutex_unlock(&s->lock);return ret;
}
int bili_av_audio(struct bili_av *s,const uint8_t *p,size_t n,uint32_t ts){
  if(n<2)return -EPROTO;
  if((p[0]>>4)!=10)return -ENOTSUP;
  pthread_mutex_lock(&s->lock);int ret=0;if(stopped(s)){ret=s->error?s->error:-ECANCELED;goto done;}
  if(p[1]==0){AACFrameInfo info;ret=audio_config(p+2,n-2,&info);if(ret)goto done;if(n-2>sizeof(s->asc)){ret=-ENOTSUP;goto done;}
    if(n-2!=s->asc_size||memcmp(s->asc,p+2,n-2)){s->stats.audio_skip+=s->audio.count;clear(&s->audio);memcpy(s->asc,p+2,n-2);s->asc_size=n-2;s->audio_generation++;s->audio_ready=false;}goto done;}
  if(p[1]!=1||n<3||!s->asc_size){ret=-EPROTO;goto done;}if(n-2>AUDIO_LIMIT){ret=-EFBIG;goto done;}
  anchor(s,ts);
  /* Queue overrun changes the media position, not AAC configuration or PCM
   * ownership. Drop only old compressed packets, keeping the speaker running. */
  if(s->audio.count>=AUDIO_QUEUE){while(s->audio.count>16){discard_head(&s->audio);s->stats.audio_skip++;}}
  ret=push(&s->audio,p+2,n-2,ts,s->audio_generation,NULL,0);if(!ret)pthread_cond_broadcast(&s->ready);
done:pthread_mutex_unlock(&s->lock);return ret;
}
int bili_av_error(struct bili_av *s){pthread_mutex_lock(&s->lock);int ret=s->error;pthread_mutex_unlock(&s->lock);return ret;}
void bili_av_stats(struct bili_av *s,struct bili_av_stats *stats){pthread_mutex_lock(&s->lock);*stats=s->stats;pthread_mutex_unlock(&s->lock);}
void bili_av_destroy(struct bili_av *s){if(!s)return;pthread_mutex_lock(&s->lock);s->stopping=true;pthread_cond_broadcast(&s->ready);pthread_mutex_unlock(&s->lock);if(s->video_started)pthread_join(s->video_thread,NULL);if(s->audio_started)pthread_join(s->audio_thread,NULL);clear(&s->video);clear(&s->audio);pthread_cond_destroy(&s->ready);pthread_mutex_destroy(&s->lock);free(s);}
