#include "bili_av.h"
#include "bili_flv.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
struct probe {FILE *file;struct bili_av *av;atomic_bool cancel;uint64_t start;unsigned audio_samples,rate;atomic_uint pictures,writes,opens,closes;bool slow,burst;};
static unsigned decode_delay;
void test_before_decode(void){if(decode_delay)usleep(decode_delay);}
void *helix_malloc(int size){return size>0&&size<512*1024?malloc(size):NULL;}
void helix_free(void *p){free(p);}
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static void pace(struct probe *p,uint32_t ts){if(p->burst||ts>10000)return;uint64_t target=p->start+(uint64_t)ts*1000;while(now()<target)usleep(1000);}
static int frame(void *ctx,const uint8_t *const planes[3],const int stride[2],int w,int h,uint64_t pts){struct probe *p=ctx;assert(planes[0]&&planes[1]&&planes[2]&&stride[0]>=w&&w==800&&h==450);(void)pts;atomic_fetch_add(&p->pictures,1);if(p->slow)usleep(200000);return 0;}
static int aopen(void *ctx,unsigned rate){struct probe *p=ctx;p->rate=rate;p->audio_samples=0;atomic_fetch_add(&p->opens,1);assert(rate==48000);return 0;}
static int awrite(void *ctx,const int16_t *pcm,unsigned frames,unsigned channels){struct probe *p=ctx;assert(pcm&&frames==1024&&channels==1);p->audio_samples+=frames;atomic_fetch_add(&p->writes,1);usleep((uint64_t)frames*1000000/p->rate);return 0;}
static unsigned atime(void *ctx){struct probe *p=ctx;return (uint64_t)p->audio_samples*1000/p->rate;}
static void aclose(void *ctx){atomic_fetch_add(&((struct probe*)ctx)->closes,1);}
static int read_file(void *ctx,uint8_t *p,size_t n){return fread(p,1,n,((struct probe*)ctx)->file);}
static int video(void *ctx,const uint8_t *p,size_t n,uint32_t ts){struct probe *s=ctx;pace(s,ts);return bili_av_video(s->av,p,n,ts);}
static int audio(void *ctx,const uint8_t *p,size_t n,uint32_t ts){struct probe *s=ctx;pace(s,ts);return bili_av_audio(s->av,p,n,ts);}
int main(int argc,char **argv){assert(argc==2);for(unsigned pass=0;pass<4;pass++){
  decode_delay=pass==2?1200000:0;
  struct probe p={.file=fopen(argv[1],"rb"),.slow=pass==0,.burst=pass==3};assert(p.file);atomic_init(&p.cancel,false);atomic_init(&p.pictures,0);atomic_init(&p.writes,0);atomic_init(&p.opens,0);atomic_init(&p.closes,0);
  struct bili_av_ops ops={frame,aopen,awrite,atime,aclose};p.av=bili_av_create(&ops,&p,&p.cancel);assert(p.av);p.start=now();
  struct bili_flv flv={.read=read_file,.video=video,.audio=audio,.opaque=&p,.managed_buffers=true};int ret=bili_flv_run(&flv);assert(ret==0||ret==-EPIPE);usleep(pass==2?3500000:500000);assert(!bili_av_error(p.av));
  struct bili_av_stats stats;bili_av_stats(p.av,&stats);assert(stats.audio_frames>(p.burst?0:100)&&atomic_load(&p.pictures)>0);
  if(p.slow)assert(stats.resync>0&&stats.decode_skip>0);
  if(pass==2)assert(stats.resync>0&&stats.displayed>0);
  if(p.burst)assert(stats.audio_skip>0);
  assert(atomic_load(&p.opens)==1); /* Overflow never reopens the speaker. */
  /* Config replacement, bounded audio overrun and stop while a packet is active. */
  uint8_t bad[]={0xaf,0,0x2a,0x10};assert(bili_av_audio(p.av,bad,sizeof(bad),0)==-ENOTSUP);
  atomic_store(&p.cancel,true);bili_av_destroy(p.av);assert(atomic_load(&p.opens)==atomic_load(&p.closes));free(flv.tag);free(flv.annexb);fclose(p.file);
  printf("PASS live single decoder AAC/PCM pass=%u: video=%u shown=%u audio=%u skip=%u resync=%u lag=%u audio_skip=%u; one audio open; stop joins and closes audio\n",pass,stats.decoded,stats.displayed,stats.audio_frames,stats.decode_skip,stats.resync,stats.lag,stats.audio_skip);
}return 0;}
