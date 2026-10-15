#include <local_video_media.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
struct probe {uint16_t pixels[640*360];unsigned frames,samples,rate,w,h;unsigned long long video_energy,audio_energy;};
static int no_h264(void *p,const uint8_t *const y[3],const int st[2],int w,int h){(void)p;(void)y;(void)st;(void)w;(void)h;abort();}
static uint16_t *buffer(void *p,unsigned w,unsigned h){struct probe *v=p;assert(w==320&&h==180);v->w=w;v->h=h;return v->pixels;}
static int publish(void *p,const uint16_t *data,unsigned w,unsigned h){struct probe *v=p;assert(data==v->pixels&&w==v->w&&h==v->h);v->frames++;for(unsigned i=0;i<w*h;i++)v->video_energy+=data[i];return 0;}
static int audio_open(void *p,unsigned rate,unsigned vol){struct probe *v=p;assert(rate==48000&&vol==40);v->rate=rate;return 0;}
static int audio(void *p,const int16_t *a,unsigned n,unsigned c){struct probe *v=p;assert(n==1024&&c==1);usleep((uint64_t)n*1000000/v->rate);v->samples+=n;for(unsigned i=0;i<n*c;i++)v->audio_energy+=a[i]<0?-a[i]:a[i];return 0;}
static int pause_audio(void *p,bool b){(void)p;(void)b;return 0;}
static int volume(void *p,unsigned n){(void)p;(void)n;return 0;}
static int drain(void *p){(void)p;return 0;}
static unsigned clock_audio(void *p){struct probe *v=p;return (uint64_t)v->samples*1000/v->rate;}
static void close_audio(void *p){(void)p;}
static void *run(void *p){return (void *)(intptr_t)local_video_run(p);}
int main(int argc,char **argv){assert(argc==2);struct local_video_ops ops={no_h264,audio_open,audio,pause_audio,volume,drain,clock_audio,close_audio,buffer,publish};
for(int i=0;i<2;i++){struct probe *p=calloc(1,sizeof(*p));struct local_video *s=local_video_create(argv[1],&ops,p);assert(s);assert(!local_video_run(s));struct local_video_snapshot state;local_video_snapshot(s,&state);assert(state.mjpeg&&state.has_audio&&p->frames>=30&&p->samples>140000&&p->video_energy>1000000&&p->audio_energy>1000000);printf("PASS MP4 MJPEG frames=%u PCM=%u JPEG_energy=%llu audio_energy=%llu\n",p->frames,p->samples,p->video_energy,p->audio_energy);local_video_destroy(s);free(p);}
struct probe *p=calloc(1,sizeof(*p));struct local_video *s=local_video_create(argv[1],&ops,p);pthread_t th;assert(!pthread_create(&th,NULL,run,s));usleep(200000);local_video_pause(s,true);usleep(100000);local_video_volume(s,75);local_video_pause(s,false);usleep(100000);local_video_cancel(s);void *ret;pthread_join(th,&ret);assert((intptr_t)ret==-ECANCELED);local_video_destroy(s);free(p);
puts("PASS MP4 native MJPEG/AAC, pause/resume/cancel");return 0;}
