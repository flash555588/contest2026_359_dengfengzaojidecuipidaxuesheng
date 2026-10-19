/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_zh_q8.h"
#include "fixture.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __riscv
#include "glass_music_pcm.h"
#endif
extern const unsigned char zh_q8_blob_0[],zh_q8_blob_0_end[],zh_q8_blob_1[],zh_q8_blob_1_end[];
extern const unsigned char zh_q8_blob_2[],zh_q8_blob_2_end[],zh_q8_blob_3[],zh_q8_blob_3_end[];
extern void qpk_dl_sha256(const void *,size_t,uint8_t[32]);
struct zh_sink {int mode;zh_q8_cancel_fn cancel;void *user;};
static bool zh_cancel(void *opaque)
{struct zh_sink *p=opaque;return p->cancel&&p->cancel(p->user);}
static int zh_pcm(void *opaque,const float *samples,size_t n)
{
  struct zh_sink *p=opaque;uint8_t hash[32];char hex[65];qpk_dl_sha256(samples,n*sizeof(float),hash);
  for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",hash[i]);
  printf("[zhq8] pcm_sha256=%s\n",hex);fflush(stdout);usleep(50000);
#ifdef __riscv
  if(p->mode&1){
    struct music_pcm pcm;int ret=music_pcm_try_open(&pcm,22050,55);if(ret)return ret;
    for(size_t at=0;at<n&&!ret;){
      if(zh_cancel(p)){ret=-ECANCELED;break;}
      int16_t block[512];size_t count=n-at;if(count>512)count=512;
      for(size_t i=0;i<count;i++){float x=samples[at+i];if(x>1)x=1;if(x<-1)x=-1;block[i]=(int16_t)(x*32767);}
      ret=music_pcm_write(&pcm,block,count,1);at+=count;
    }
    if(!ret)ret=music_pcm_drain(&pcm);music_pcm_close(&pcm);return ret;
  }
#else
  (void)p;FILE *f=fopen("zh-q8-host.f32","wb");if(!f)return -EIO;
  int ret=fwrite(samples,sizeof(float),n,f)==n?0:-EIO;if(fclose(f)&&!ret)ret=-EIO;return ret;
#endif
  return 0;
}
int glass_zh_q8_probe(int mode,zh_q8_cancel_fn cancel,void *user)
{
  if(mode<0||mode>5)return -EINVAL;
  if(mode>=2){
    int16_t a[1344] __attribute__((aligned(16))),b[1344] __attribute__((aligned(16)));
    uint32_t seed=71;unsigned cases=0;
    for(int n=8;n<=1344;n+=8)for(unsigned trial=0;trial<4;trial++){
      int32_t reference=0;
      for(int j=0;j<n;j++){
        seed=seed*1664525u+1013904223u;a[j]=(int16_t)((int)(seed%4095)-2047);
        seed=seed*1664525u+1013904223u;b[j]=(int16_t)((int)(seed%256)-128);
        if(trial==0)a[j]=n<=256?-32767:-2047,b[j]=-128;
        reference+=(int32_t)a[j]*b[j];
      }
      if(glass_zh_q8_dot(a,b,n)!=reference)return -EBADMSG;cases++;
    }
    printf("[zhq8] pie_kernel_cases=%u result=0\n",cases);fflush(stdout);usleep(50000);
  }
  const struct zh_q8_blob blobs[]={
    {zh_q8_blob_0,(size_t)(zh_q8_blob_0_end-zh_q8_blob_0)},
    {zh_q8_blob_1,(size_t)(zh_q8_blob_1_end-zh_q8_blob_1)},
    {zh_q8_blob_2,(size_t)(zh_q8_blob_2_end-zh_q8_blob_2)},
    {zh_q8_blob_3,(size_t)(zh_q8_blob_3_end-zh_q8_blob_3)}};
  struct zh_sink sink={mode,cancel,user};struct zh_q8_stats stats;
  int backend=mode>=4?2:mode>=2?1:0;
  int ret=glass_zh_q8_synthesize(blobs,zh_q8_ids,sizeof(zh_q8_ids)/sizeof(*zh_q8_ids),backend,zh_pcm,zh_cancel,&sink,&stats);
  printf("[zhq8] result=%d duration_us=%llu latent_us=%llu decoder_us=%llu\n",ret,
    (unsigned long long)stats.duration_us,(unsigned long long)stats.latent_us,(unsigned long long)stats.decoder_us);
  fflush(stdout);usleep(50000);
  printf("[zhq8] first_pcm_us=%llu frames=%u samples=%u rate=22050 working_bytes=%u\n",
    (unsigned long long)stats.first_pcm_us,stats.frames,stats.samples,(unsigned)stats.working_bytes);
  fflush(stdout);usleep(50000);
  printf("[zhq8] conv1d_us=%llu conv1d_calls=%u convtr_us=%llu convtr_calls=%u\n",
    (unsigned long long)stats.conv1d_us,stats.conv1d_calls,(unsigned long long)stats.convtr_us,stats.convtr_calls);return ret;
}
