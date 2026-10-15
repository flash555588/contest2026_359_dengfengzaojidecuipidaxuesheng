#include <local_video_media.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
struct probe {unsigned frames,samples,rate,channels,clock_ahead; long long energy;};
static int frame(void *p,const uint8_t *const planes[3],const int s[2],int w,int h){struct probe *v=p;assert(planes[0]&&s[0]>=w&&w==320&&h==180);v->frames++;return 0;}
static int open_audio(void *p,unsigned rate,unsigned vol){struct probe *v=p;v->rate=rate;assert(vol==40);return 0;}
static int audio(void *p,const int16_t *a,unsigned n,unsigned c){struct probe *v=p;assert(c==2);usleep((uint64_t)n*1000000/v->rate);v->channels=c;v->samples+=n;for(unsigned i=0;i<n*c;i++)v->energy+=a[i]<0?-a[i]:a[i];return 0;}
static int pause_audio(void *p,bool x){(void)p;(void)x;return 0;}
static int vol_audio(void *p,unsigned x){(void)p;(void)x;return 0;}
static int drain(void *p){(void)p;return 0;}
static unsigned clock_audio(void *p){struct probe *v=p;return (uint64_t)v->samples*1000/v->rate+v->clock_ahead;}
static void close_audio(void *p){(void)p;}
static void *run(void *p){return (void *)(intptr_t)local_video_run(p);}
int main(int argc,char **argv){assert(argc==2);struct local_video_ops ops={frame,open_audio,audio,pause_audio,vol_audio,drain,clock_audio,close_audio};
for(int i=0;i<3;i++){struct probe p={0};struct local_video *v=local_video_create(argv[1],&ops,&p);assert(v);int r=local_video_run(v);printf("result=%d frames=%u samples=%u energy=%lld\n",r,p.frames,p.samples,p.energy);assert(!r);assert(p.frames>=30);assert(p.samples>120000&&p.energy>1000000);local_video_destroy(v);}
struct probe lag={.clock_ahead=1500};struct local_video *late=local_video_create(argv[1],&ops,&lag);assert(!local_video_run(late));struct local_video_snapshot state;local_video_snapshot(late,&state);assert(lag.frames>0&&state.dropped>0);printf("PASS late-video starvation: rendered=%u dropped=%u\n",lag.frames,state.dropped);local_video_destroy(late);
struct probe p={0};struct local_video *v=local_video_create(argv[1],&ops,&p);pthread_t th;assert(!pthread_create(&th,NULL,run,v));usleep(300000);local_video_pause(v,true);usleep(100000);local_video_volume(v,80);local_video_pause(v,false);usleep(100000);local_video_cancel(v);void *result;pthread_join(th,&result);assert((intptr_t)result==-ECANCELED);local_video_destroy(v);puts("PASS local AV decode, pause/resume, cancellation and repeated cleanup");return 0;}
