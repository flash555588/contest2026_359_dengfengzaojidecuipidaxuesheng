/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_nano_probe.h"
#include "glass_speech_compute.h"
#include "snt_nano.h"
#include "snt_port.h"
#include "nano_probe_ids.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <unistd.h>
#ifdef __riscv
#include "glass_music_pcm.h"
#endif
extern const unsigned char nano_front_blob[],nano_decoder_blob[];
extern void qpk_dl_sha256(const void *,size_t,uint8_t[32]);
extern size_t glass_nano_storage_size(void);
extern void glass_nano_storage_bind(void *);
static bool nano_pie;
static unsigned long nano_dot_calls;
static uint64_t nano_dot_us;
int64_t snt_now_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static int32_t nano_scalar(const int8_t *a,const int8_t *b,int n)
{int32_t sum=0;for(int i=0;i<n;i++)sum+=(int32_t)a[i]*b[i];return sum;}
#ifdef __riscv
extern int32_t glass_nano_dot_pie(const int8_t *,const int8_t *,int);
#endif
int32_t snt_dot_s8(const int8_t *a,const int8_t *b,int n)
{
  if(n<=0||n>65536)return 0;
#ifdef __riscv
  if(nano_pie&&!(n&15)&&!(((uintptr_t)a|(uintptr_t)b)&15))return glass_nano_dot_pie(a,b,n);
#endif
  return nano_scalar(a,b,n);
}
void snt_matvec_s8(const int8_t *a,const int8_t *w,int32_t *out,int rows,int n)
{
  glass_speech_compute_checkpoint();int64_t start=snt_now_us();
  for(int r=0;r<rows;r++)out[r]=snt_dot_s8(a,w+(long)r*n,n);
  nano_dot_us+=snt_now_us()-start;nano_dot_calls++;
}
int32_t snt_dot_s16s8(const int16_t *a,const int8_t *b,int n)
{int32_t sum=0;for(int i=0;i<n;i++)sum+=(int32_t)a[i]*b[i];return sum;}
void snt_matvec_s16s8(const int16_t *a,const int8_t *b,int32_t *out,int rows,int n)
{for(int r=0;r<rows;r++)out[r]=snt_dot_s16s8(a,b+(long)r*n,n);}
int snt_weights_resident(const void *p){(void)p;return 1;}
int snt_scratch_id(void){return 0;}
void snt_par_run(snt_par_fn f,int n,void *ctx)
{glass_speech_compute_checkpoint();f(0,n,ctx);}
struct nano_job {snt_nano_config config;snt_nano_stats stats;float *pcm;size_t count,capacity;int64_t started,first;};
static int nano_pcm(const float *pcm,int n,void *opaque)
{
  struct nano_job *p=opaque;
  if(n<0||(size_t)n>p->capacity-p->count)return -E2BIG;
  if(!p->first)p->first=snt_now_us()-p->started;
  memcpy(p->pcm+p->count,pcm,(size_t)n*sizeof(float));p->count+=n;return 0;
}
static int nano_body(void *opaque)
{
  struct nano_job *p=opaque;
  return snt_nano_synthesize(&p->config,nano_probe_ids,sizeof(nano_probe_ids)/sizeof(*nano_probe_ids),nano_pcm,p,&p->stats);
}
static int nano_kernel_check(void)
{
  int8_t *a=memalign(16,4096),*b=memalign(16,4096);if(!a||!b){free(a);free(b);return -ENOMEM;}
  uint32_t seed=71;unsigned cases=0;int ret=0;
  for(unsigned n=16;n<=4096;n*=2)for(unsigned trial=0;trial<32;trial++){
    for(unsigned i=0;i<n;i++){seed=seed*1664525u+1013904223u;a[i]=(int8_t)(seed>>24);seed=seed*1664525u+1013904223u;b[i]=(int8_t)(seed>>24);}
    if(trial==0)memset(a,0x80,n),memset(b,0x80,n);
    nano_pie=true;if(snt_dot_s8(a,b,n)!=nano_scalar(a,b,n)){ret=-EBADMSG;break;}cases++;
  }
  free(a);free(b);printf("[nano] kernel_cases=%u result=%d\n",cases,ret);return ret;
}
int glass_nano_probe(int mode,speech_cancel_fn cancel,void *user)
{
#if defined(__riscv)
  __asm__ volatile("csrwi 0x7f2, 1\ncsrwi 0x7f1, 1" ::: "memory");
#endif
  int ret=nano_kernel_check();if(ret)return ret;
  struct nano_job job={0};job.capacity=24000*12;
  job.config.front_blob=nano_front_blob;job.config.dec_blob=nano_decoder_blob;
  job.config.arena_size=1024*1024;job.config.arena=memalign(16,job.config.arena_size);
  job.pcm=malloc(job.capacity*sizeof(float));job.config.noise_seed=NANO_PROBE_SEED;
  size_t storage_size=glass_nano_storage_size();void *storage=memalign(16,storage_size);
  if(!job.config.arena||!job.pcm||!storage){free(job.config.arena);free(job.pcm);free(storage);return -ENOMEM;}
  memset(storage,0,storage_size);glass_nano_storage_bind(storage);
  nano_pie=mode!=0;nano_dot_calls=0;nano_dot_us=0;job.started=snt_now_us();
  ret=glass_speech_compute_run(nano_body,&job,cancel,user);
  int64_t elapsed=snt_now_us()-job.started;
  if(!ret&&(!job.count||job.count!=(size_t)job.stats.samples))ret=-EBADMSG;
  uint8_t hash[32]={0};if(!ret){qpk_dl_sha256(job.pcm,job.count*sizeof(float),hash);
    for(size_t i=0;i<job.count;i++)if(!isfinite(job.pcm[i])){ret=-EBADMSG;break;}}
  printf("[nano] backend=%s result=%d elapsed_us=%lld first_pcm_us=%lld samples=%u rate=24000 arena_peak=%u matvec_us=%llu matvec_calls=%lu\n",
    nano_pie?"pie":"scalar",ret,(long long)elapsed,(long long)job.first,(unsigned)job.count,(unsigned)job.stats.arena_peak,(unsigned long long)nano_dot_us,nano_dot_calls);
  char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",hash[i]);
  /* The USB console has a bounded TX queue. Drain the metrics before the
   * integrity record; this delay is outside both inference measurements. */
  fflush(stdout);usleep(50000);
  printf("[nano] pcm_sha256=%s\n",hex);fflush(stdout);
#ifndef __riscv
  if(!ret){FILE *f=fopen("nano-host.f32","wb");if(f){fwrite(job.pcm,sizeof(float),job.count,f);fclose(f);}}
#else
  if(!ret&&mode==2){
    struct music_pcm pcm;ret=music_pcm_try_open(&pcm,24000,55);
    if(!ret){
      for(size_t at=0;at<job.count&&!ret;){
        if(cancel&&cancel(user)){ret=-ECANCELED;break;}
        int16_t block[512];size_t n=job.count-at;if(n>512)n=512;
        for(size_t i=0;i<n;i++){float x=job.pcm[at+i];if(x>1)x=1;if(x<-1)x=-1;block[i]=(int16_t)(x*32767);}
        ret=music_pcm_write(&pcm,block,n,1);at+=n;
      }
      if(!ret)ret=music_pcm_drain(&pcm);
      music_pcm_close(&pcm);
    }
    printf("[nano] playback_result=%d\n",ret);
  }
#endif
  free(storage);free(job.pcm);free(job.config.arena);return ret;
}
