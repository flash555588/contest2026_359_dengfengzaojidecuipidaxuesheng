/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_music_pv_bank.h"
#ifdef __NuttX__
#include "glass_music_pv_import.h"
#endif
#include <pthread.h>
#include <time.h>
struct music_pv_sd {
  pthread_mutex_t lock;pthread_cond_t wake;pthread_t thread;
  struct jpv_bank bank;struct jpv_reader reader;
  lv_image_dsc_t images[2];uint16_t *pixels[2];
  char path[128],name[128],artist[96];
  unsigned style,position,slot,front,frame,shown,reads,displayed,skipped,fps,frames;
  uint64_t total_us,max_us,io_us,check_us;
  int error;bool started,quit,busy,pending,ready,has_frame;
};
static uint64_t music_pv_sd_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static bool music_pv_sd_cancel(void *arg)
{struct music_pv_sd *s=arg;pthread_mutex_lock(&s->lock);bool quit=s->quit;pthread_mutex_unlock(&s->lock);return quit;}
static void *music_pv_sd_worker(void *arg)
{
  struct music_pv_sd *s=arg;pthread_mutex_lock(&s->lock);
  for(;;){
    while(!s->pending&&!s->quit)pthread_cond_wait(&s->wake,&s->lock);
    if(s->quit)break;
    unsigned ms=s->position,slot=s->slot;s->pending=false;pthread_mutex_unlock(&s->lock);
    uint64_t began=music_pv_sd_us();
    int ret=0;
    if(s->bank.fd<0){
      ret=jpv_open(&s->bank,s->path,s->name,s->artist,s->style);
      if(ret==-ENOENT){
        struct stat st;
        if(stat(MUSIC_PV_FAST_ROOT,&st)<0){
          snprintf(s->path,sizeof(s->path),MUSIC_PV_BANK_ROOT "/%08lx-%02u.jpv",(unsigned long)jpv_key(s->name,s->artist),s->style);
          ret=jpv_open(&s->bank,s->path,s->name,s->artist,s->style);
        }
      }
    }
    unsigned frame=ret?0:(uint64_t)ms*s->bank.fps/1000;
    if(!ret)ret=jpv_frame(&s->bank,frame,s->pixels[slot],&s->reader);
    uint64_t elapsed=music_pv_sd_us()-began;pthread_mutex_lock(&s->lock);
    s->reads++;s->total_us+=elapsed;if(elapsed>s->max_us)s->max_us=elapsed;
    s->io_us+=s->reader.io_us;s->check_us+=s->reader.check_us;
    s->error=ret;s->fps=s->bank.fps;s->frames=s->bank.frames;s->frame=frame;s->ready=true;s->busy=false;
  }
  pthread_mutex_unlock(&s->lock);
  if(s->bank.fd>=0)close(s->bank.fd);jpv_reader_release(&s->reader);return NULL;
}
static void music_pv_sd_release(struct music_pv_ui *p)
{
  struct music_pv_sd *s=p->sd;if(!s)return;
  if(p->sd_image){lv_obj_add_flag(p->sd_image,LV_OBJ_FLAG_HIDDEN);lv_image_set_src(p->sd_image,NULL);}
  if(s->started){pthread_mutex_lock(&s->lock);s->quit=true;pthread_cond_signal(&s->wake);pthread_mutex_unlock(&s->lock);pthread_join(s->thread,NULL);}
  for(unsigned i=0;i<2;i++){lv_image_cache_drop(&s->images[i]);free(s->pixels[i]);}
  pthread_cond_destroy(&s->wake);pthread_mutex_destroy(&s->lock);free(s);p->sd=NULL;
}
static struct music_pv_sd *music_pv_sd_create(struct music_pv_ui *p,const struct music_now_playing *now)
{
  struct music_pv_sd *s=calloc(1,sizeof(*s));if(!s)return NULL;
  s->bank.fd=-1;
  pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->wake,NULL);p->sd=s;
  s->reader.cancel=music_pv_sd_cancel;s->reader.opaque=s;
  snprintf(s->name,sizeof(s->name),"%s",now->name);snprintf(s->artist,sizeof(s->artist),"%s",now->artist);
  s->style=p->style_index;
  snprintf(s->path,sizeof(s->path),MUSIC_PV_FAST_ROOT "/%08lx-%02u.jpv",(unsigned long)jpv_key(s->name,s->artist),s->style);
  for(unsigned i=0;i<2;i++){
    s->pixels[i]=malloc(JPV_BYTES);if(!s->pixels[i]){music_pv_sd_release(p);return NULL;}
    s->images[i].header.magic=LV_IMAGE_HEADER_MAGIC;s->images[i].header.cf=LV_COLOR_FORMAT_RGB565;
    s->images[i].header.w=JPV_WIDTH;s->images[i].header.h=JPV_HEIGHT;s->images[i].header.stride=JPV_WIDTH*2;
    s->images[i].data=(const uint8_t *)s->pixels[i];s->images[i].data_size=JPV_BYTES;
  }
  pthread_attr_t attr;pthread_attr_init(&attr);
#ifdef __NuttX__
  pthread_attr_setstacksize(&attr,8192);
  /* SD decoding must not preempt audio refill or the desktop. */
  pthread_attr_setinheritsched(&attr,PTHREAD_EXPLICIT_SCHED);
  struct sched_param priority={.sched_priority=90};pthread_attr_setschedparam(&attr,&priority);
#endif
  int ret=pthread_create(&s->thread,&attr,music_pv_sd_worker,s);pthread_attr_destroy(&attr);
  if(ret){music_pv_sd_release(p);return NULL;}s->started=true;
  return s;
}
static bool music_pv_sd_refresh(struct music_pv_ui *p,const struct music_now_playing *now)
{
  if(p->sd_track!=now->track_id||p->sd_style!=p->style_index){
    music_pv_sd_release(p);p->sd_track=now->track_id;p->sd_style=p->style_index;p->sd_failed=false;
  }
  if(g_glass.reduced_motion||!now->name[0]||(now->state!=MUSIC_PLAYING&&now->state!=MUSIC_PAUSED)){
    music_pv_sd_release(p);return false;
  }
  if(p->sd_failed)return false;
  struct music_pv_sd *s=p->sd?p->sd:music_pv_sd_create(p,now);
  if(!s){p->sd_failed=true;return false;}
  pthread_mutex_lock(&s->lock);
  if(s->ready){
    unsigned desired=(uint64_t)now->position_ms*s->fps/1000;
    if(s->error){
      int error=s->error;pthread_mutex_unlock(&s->lock);
      /* Missing/finished/corrupt assets fail once per selection, never spin. */
      if(error!=-ENOENT&&error!=-ERANGE)fprintf(stderr,"[music-pv] SD fallback %d\n",error);
      p->sd_failed=true;music_pv_sd_release(p);return false;
    }
    bool future=s->frame==desired+1&&now->state==MUSIC_PLAYING;
    if(s->frame<=desired&&desired-s->frame<=2){
      s->front=s->slot;s->shown=s->frame;s->has_frame=true;s->displayed++;
      lv_image_cache_drop(&s->images[s->front]);lv_image_set_src(p->sd_image,&s->images[s->front]);
      lv_obj_remove_flag(p->sd_image,LV_OBJ_FLAG_HIDDEN);
    }else if(!future)s->skipped++;
    if(!future)s->ready=false;
  }
  unsigned desired=s->fps?(uint64_t)now->position_ms*s->fps/1000:0;
  bool current=s->has_frame&&s->shown<=desired&&desired-s->shown<=2;
  unsigned target=desired+(current&&now->state==MUSIC_PLAYING?1:0);
  if(!s->busy&&!s->ready&&(!s->frames||target<s->frames)&&(!s->has_frame||s->shown!=target)){
    s->slot=1-s->front;
    s->position=s->fps?((uint64_t)target*1000+s->fps-1)/s->fps:now->position_ms;
    s->pending=s->busy=true;pthread_cond_signal(&s->wake);
  }
  bool visible=s->has_frame;
  /* A seek/track change never displays an unrelated old frame. */
  if(visible&&(s->shown>desired||desired-s->shown>2)){visible=false;lv_obj_add_flag(p->sd_image,LV_OBJ_FLAG_HIDDEN);}
  pthread_mutex_unlock(&s->lock);return visible;
}
