/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_zh_q8.h"
#include "glass_speech_compute.h"
#include "snt_front_q8.h"
#include "snt_piperlite_q8.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define ZH_MAX_IDS 128
#define ZH_MAX_FRAMES 512
#define ZH_WORK_LIMIT (8u*1024u*1024u)
#define ZH_TILE 64
struct zh_call {
  snt_front_q8_model front;snt_piperlite_q8_model decoder;
  const int32_t *ids;int32_t durations[ZH_MAX_IDS];int n,stage;
  long frames;void *arena;size_t arena_bytes;float *latent,*audio;
  int16_t *packed,*packed_second,*weights;size_t packed_bytes,weights_bytes;bool dual;
  struct zh_q8_stats *stats;
};
static uint64_t zh_now(void);
int32_t glass_zh_q8_dot(const int16_t *a,const int16_t *b,int n)
{
#ifdef __riscv
  extern int32_t glass_zh_q8_dot_pie(const int16_t *,const int16_t *,int);
  if(n>0&&!(n&7)&&!(((uintptr_t)a|(uintptr_t)b)&15))return glass_zh_q8_dot_pie(a,b,n);
#endif
  int32_t sum=0;for(int i=0;i<n;i++)sum+=(int32_t)a[i]*b[i];return sum;
}
struct zh_tiles {
  int16_t *outq,*packed;float *outf;const int16_t *x,*weights,*residual;
  const float *scales,*bias;
  int in_ch,out_ch,T,source_T,K,dil,stride,pad,fan,padded,kind,start,end;
  float s_in,s_out,res_scale;
};
static int zh_tiles_body(void *opaque)
{
  const struct zh_tiles *a=opaque;
  for(int at=a->start;at<a->end;at+=ZH_TILE){
    glass_speech_compute_checkpoint();int count=a->end-at;if(count>ZH_TILE)count=ZH_TILE;
    for(int j=0;j<count;j++){
      int t=at+j;int16_t *row=a->packed+(size_t)j*a->padded;
      int taps=a->stride?2:a->K;
      for(int ic=0;ic<a->in_ch;ic++)for(int k=0;k<taps;k++){
        int source=a->stride?(t+a->pad)/a->stride-k:t+a->dil*(k-a->K/2);
        row[ic*taps+k]=source>=0&&source<a->source_T?a->x[(size_t)ic*a->source_T+source]:0;
      }
      for(int j=a->fan;j<a->padded;j++)row[j]=0;
    }
    for(int oc=0;oc<a->out_ch;oc++){
      float mm=a->s_in*a->scales[oc],cc=a->bias[oc];
      if(a->kind==0){mm/=a->s_out;cc/=a->s_out;}
      for(int j=0;j<count;j++){
        int phase=a->stride?(at+j+a->pad)%a->stride:0;
        int32_t sum=glass_zh_q8_dot(a->packed+(size_t)j*a->padded,a->weights+((size_t)phase*a->out_ch+oc)*a->padded,a->padded);
        size_t index=(size_t)oc*a->T+at+j;
        float value=(float)sum*mm+cc;
        if(a->residual)value+=(float)a->residual[index]*a->res_scale;
        if(a->kind==0){long q=lrintf(value);if(q>2047)q=2047;if(q< -2047)q=-2047;a->outq[index]=(int16_t)q;}
        else if(a->kind==1)a->outf[index]+=value;
        else a->outf[index]=value;
      }
    }
  }
  return 0;
}
static bool zh_dispatch(struct zh_tiles a,const int8_t *w)
{
  struct zh_call *p=glass_speech_compute_argument();
  if(!p||!p->packed||p->stage!=2||a.in_ch<1||a.K<1||a.T<1)return false;
  if(a.stride&&a.K!=2*a.stride)return false;
  a.fan=a.in_ch*(a.stride?2:a.K);a.padded=(a.fan+7)&~7;
  int phases=a.stride?a.stride:1;
  if(a.padded>1344||(size_t)ZH_TILE*a.padded*sizeof(int16_t)>p->packed_bytes||
     (size_t)phases*a.out_ch*a.padded*sizeof(int16_t)>p->weights_bytes)return false;
  uint64_t start=zh_now();
  for(int phase=0;phase<phases;phase++)for(int oc=0;oc<a.out_ch;oc++){
    int16_t *weights=p->weights+((size_t)phase*a.out_ch+oc)*a.padded;
    for(int j=0;j<a.fan;j++)weights[j]=a.stride?
      w[((size_t)(j/2)*a.out_ch+oc)*a.K+phase+(j%2)*a.stride]:w[(size_t)oc*a.fan+j];
    for(int j=a.fan;j<a.padded;j++)weights[j]=0;
  }
  a.packed=p->packed;a.weights=p->weights;a.start=0;a.end=a.T;
  if(p->dual&&a.T>=256&&(size_t)a.T*a.fan*a.out_ch>=1000000){
    struct zh_tiles right=a;a.end=a.T/2;right.start=a.end;right.packed=p->packed_second;
    glass_speech_compute_parallel(zh_tiles_body,&a,&right);
  }else zh_tiles_body(&a);
  if(a.stride){p->stats->convtr_us+=zh_now()-start;p->stats->convtr_calls++;}
  else{p->stats->conv1d_us+=zh_now()-start;p->stats->conv1d_calls++;}
  return true;
}
bool glass_zh_q8_conv1d_stage(int16_t *outq,float *outf,const int16_t *x,const int8_t *w,
  const float *scales,const float *bias,int in_ch,int out_ch,int T,int K,int dil,
  float s_in,float s_out,const int16_t *residual,float res_scale,int kind)
{
  struct zh_tiles a={.outq=outq,.outf=outf,.x=x,.scales=scales,.bias=bias,.in_ch=in_ch,.out_ch=out_ch,
    .T=T,.source_T=T,.K=K,.dil=dil,.s_in=s_in,.s_out=s_out,.residual=residual,.res_scale=res_scale,.kind=kind};
  return zh_dispatch(a,w);
}
bool glass_zh_q8_convtr_stage(int16_t *out,const int16_t *x,const int8_t *w,const float *scales,const float *bias,
  int in_ch,int out_ch,int T,int K,int stride,int pad,int L,float s_in,float s_out)
{
  struct zh_tiles a={.outq=out,.x=x,.scales=scales,.bias=bias,.in_ch=in_ch,.out_ch=out_ch,
    .T=L,.source_T=T,.K=K,.stride=stride,.pad=pad,.s_in=s_in,.s_out=s_out};
  return zh_dispatch(a,w);
}
static uint64_t zh_now(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static size_t zh_align(size_t n){return (n+15u)&~(size_t)15u;}
static int zh_body(void *opaque)
{
  struct zh_call *p=opaque;
  if(p->stage==0){p->frames=snt_front_q8_durations(&p->front,p->ids,p->n,1.0f,p->durations,p->arena,p->arena_bytes);return p->frames<0?(int)p->frames:0;}
  if(p->stage==1)return snt_front_q8_latent(&p->front,p->ids,p->durations,p->n,p->frames,p->latent,p->arena,p->arena_bytes);
  return snt_piperlite_q8_synthesize(&p->decoder,p->latent,(int)p->frames,p->audio,p->arena,p->arena_bytes);
}
int glass_zh_q8_synthesize(const struct zh_q8_blob blobs[4],const int32_t *ids,int n,int backend,
  zh_q8_pcm_fn pcm,zh_q8_cancel_fn cancel,void *user,struct zh_q8_stats *stats)
{
  if(!blobs||!ids||!pcm||!stats||n<1||n>ZH_MAX_IDS||backend<0||backend>2)return -EINVAL;
  memset(stats,0,sizeof(*stats));uint64_t start=zh_now(),at=start;
  struct zh_call *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
  p->stats=stats;
  int ret=snt_front_q8_init(&p->front,blobs[1].data,blobs[1].bytes,blobs[0].data,blobs[0].bytes);
  if(!ret)ret=snt_piperlite_q8_init(&p->decoder,blobs[3].data,blobs[3].bytes,blobs[2].data,blobs[2].bytes);
  if(ret)goto done;
  if(p->front.a_out!=p->decoder.in_ch||p->front.a_out>512||p->decoder.c0>512||n>p->front.d_max_tokens){ret=-EINVAL;goto done;}
  for(int i=0;i<n;i++)if(ids[i]<0||ids[i]>=p->front.d_vocab||ids[i]>=p->front.a_vocab){ret=-EINVAL;goto done;}
  p->ids=ids;p->n=n;p->arena_bytes=zh_align(snt_front_q8_duration_arena_bytes(&p->front,n));
  if(!p->arena_bytes||p->arena_bytes>ZH_WORK_LIMIT-sizeof(*p)){ret=-E2BIG;goto done;}
  p->arena=aligned_alloc(16,p->arena_bytes);if(!p->arena){ret=-ENOMEM;goto done;}
  stats->working_bytes=p->arena_bytes+sizeof(*p);
  ret=glass_speech_compute_run(zh_body,p,cancel,user);stats->duration_us=zh_now()-at;
  if(ret)goto done;
  if(p->frames<1||p->frames>ZH_MAX_FRAMES){ret=-E2BIG;goto done;}
  free(p->arena);p->arena=NULL;
  size_t front=snt_front_q8_latent_arena_bytes(&p->front,n,p->frames);
  size_t decoder=snt_piperlite_q8_arena_bytes(&p->decoder,(int)p->frames);
  p->arena_bytes=zh_align(front>decoder?front:decoder);
  size_t latent=zh_align((size_t)p->front.a_out*p->frames*sizeof(float));
  size_t audio=(size_t)p->frames*SNT_PIPERLITE_Q8_HOP*sizeof(float);
  size_t total=p->arena_bytes+latent+audio;
  if(!front||!decoder||total>ZH_WORK_LIMIT-sizeof(*p)){ret=-E2BIG;goto done;}
  p->arena=aligned_alloc(16,zh_align(total));if(!p->arena){ret=-ENOMEM;goto done;}
  if(total+sizeof(*p)>stats->working_bytes)stats->working_bytes=total+sizeof(*p);
  if(backend){
    int channels[]={p->decoder.in_ch,p->decoder.c1,p->decoder.c2,p->decoder.c3};
    int outputs[]={p->decoder.c0,p->decoder.c1,p->decoder.c2,p->decoder.c3};
    size_t packed=0,weights=0;
    for(unsigned i=0;i<4;i++){
      size_t row=(size_t)((channels[i]*7+7)&~7)*sizeof(int16_t);
      if(row*ZH_TILE>packed)packed=row*ZH_TILE;
      if(row*outputs[i]>weights)weights=row*outputs[i];
    }
    int up_in[]={p->decoder.c0,p->decoder.c1,p->decoder.c2};
    int up_out[]={p->decoder.c1,p->decoder.c2,p->decoder.c3};
    int strides[]={8,8,4};
    for(unsigned i=0;i<3;i++){
      size_t bytes=(size_t)strides[i]*up_out[i]*((up_in[i]*2+7)&~7)*sizeof(int16_t);
      if(bytes>weights)weights=bytes;
    }
    size_t copies=backend==2?2:1;
    /* Larger chunks fall back to scalar rather than exceeding the job budget. */
    if(copies*packed+weights<=ZH_WORK_LIMIT-total-sizeof(*p)){
      p->packed=aligned_alloc(16,zh_align(copies*packed+weights));if(!p->packed){ret=-ENOMEM;goto done;}
      p->packed_second=(int16_t *)((unsigned char *)p->packed+packed);
      p->weights=(int16_t *)((unsigned char *)p->packed+copies*packed);p->weights_bytes=weights;
      p->dual=backend==2;p->packed_bytes=packed;stats->working_bytes+=copies*packed+weights;
    }
  }
  p->latent=(float *)((unsigned char *)p->arena+p->arena_bytes);
  p->audio=(float *)((unsigned char *)p->latent+latent);
  p->stage=1;at=zh_now();ret=glass_speech_compute_run(zh_body,p,cancel,user);stats->latent_us=zh_now()-at;
  if(ret)goto done;
  p->stage=2;at=zh_now();ret=glass_speech_compute_run(zh_body,p,cancel,user);stats->decoder_us=zh_now()-at;
  if(ret)goto done;
  stats->frames=(unsigned)p->frames;stats->samples=(unsigned)p->frames*SNT_PIPERLITE_Q8_HOP;
  for(unsigned i=0;i<stats->samples;i++)if(!isfinite(p->audio[i])){ret=-EBADMSG;goto done;}
  if(cancel&&cancel(user)){ret=-ECANCELED;goto done;}
  stats->first_pcm_us=zh_now()-start;ret=pcm(user,p->audio,stats->samples);
done:
  free(p->packed);free(p->arena);free(p);return ret;
}
