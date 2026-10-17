/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_SPEECH_CONV_H
#define GLASS_SPEECH_CONV_H
#include "glass_speech_compute.h"

/* The reference walks a complete output row for every weight. At the last
 * decoder stage a row exceeds the data cache. Restrict each walk to 256
 * samples, retaining the exact input-channel/kernel accumulation order for
 * every sample. No new arena, allocation, approximation or fast-math flag.
 * Input and output are distinct in all front/decoder call sites. */
static void glass_speech_conv_range(float *out, const float *x, const float *w,
                               const float *b, int in_ch, int out_ch, int T,
                               int K, int dil, int accumulate,int first,int last)
{
  int pad = dil * (K / 2);
  for (int oc = first; oc < last; oc++)
    {
      float *orow = out + (long)oc * T;
      for (int start = 0; start < T; start += 256)
        {
          glass_speech_compute_checkpoint();
          int end = T - start > 256 ? start + 256 : T;
          for (int t = start; t < end; t++)
            if (accumulate) orow[t] += b[oc];
            else orow[t] = b[oc];
          for (int ic = 0; ic < in_ch; ic++)
            {
              const float *xrow = x + (long)ic * T;
              const float *wrow = w + ((long)oc * in_ch + ic) * K;
              for (int k = 0; k < K; k++)
                {
                  float wv = wrow[k];
                  int off = k * dil - pad;
                  int lo = off < 0 ? -off : 0;
                  int hi = off > 0 ? T - off : T;
                  if (lo < start) lo = start;
                  if (hi > end) hi = end;
                  if (wv != 0.0f)
                    {
                      /* Four independent samples expose instruction-level
                       * parallelism to the scalar FPU. Each sample retains
                       * the reference's exact ic/k accumulation order. */
                      int t=lo;
                      for (; t+3<hi; t+=4)
                        {
                          float a=orow[t],c=orow[t+1];
                          float d=orow[t+2],e=orow[t+3];
                          a+=wv*xrow[t+off];c+=wv*xrow[t+off+1];
                          d+=wv*xrow[t+off+2];e+=wv*xrow[t+off+3];
                          orow[t]=a;orow[t+1]=c;
                          orow[t+2]=d;orow[t+3]=e;
                        }
                      for (; t<hi; t++)orow[t]+=wv*xrow[t+off];
                    }
                }
            }
        }
    }
}
struct glass_conv_call {
  float *out;const float *x,*w,*b;
  int in_ch,out_ch,T,K,dil,accumulate,first,last;
};
static int glass_conv_task(void *opaque)
{
  struct glass_conv_call *p=opaque;
  glass_speech_conv_range(p->out,p->x,p->w,p->b,p->in_ch,p->out_ch,
    p->T,p->K,p->dil,p->accumulate,p->first,p->last);return 0;
}
static void glass_speech_conv1d(float *out,const float *x,const float *w,
  const float *b,int in_ch,int out_ch,int T,int K,int dil,int accumulate)
{
  struct glass_conv_call left={out,x,w,b,in_ch,out_ch,T,K,dil,accumulate,0,out_ch};
  /* Avoid thread overhead for tiny layers and single-channel output. */
  if(out_ch>=2&&(long long)in_ch*out_ch*T*K>=200000){
    struct glass_conv_call right=left;left.last=out_ch/2;right.first=left.last;
    glass_speech_compute_parallel(glass_conv_task,&left,&right);
  }else glass_conv_task(&left);
}
#endif
