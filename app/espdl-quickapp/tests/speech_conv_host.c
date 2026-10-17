/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glass_speech_conv.h"
void reference_conv(float *, const float *, const float *, const float *,
                    int, int, int, int, int, int);
static unsigned seed = 71;
static float value(void)
{
  seed = seed * 1664525u + 1013904223u;
  return (seed % 17 == 0) ? -0.0f : ((int)(seed % 2001) - 1000) / 1024.0f;
}
int main(void)
{
  const int lengths[] = {1,2,3,7,31,127,255,256,257,511,512,513,4097,20992};
  const int kernels[] = {1,3,5,7};
  const int dilations[] = {1,2,3,6,12};
  unsigned cases = 0;
  for (unsigned ti=0; ti<sizeof(lengths)/sizeof(*lengths); ti++)
    for (unsigned ki=0; ki<sizeof(kernels)/sizeof(*kernels); ki++)
      for (unsigned di=0; di<sizeof(dilations)/sizeof(*dilations); di++)
        for (int accumulate=0; accumulate<2; accumulate++)
          {
            int T=lengths[ti], K=kernels[ki], in=5, out=3;
            size_t n=(size_t)T*out;
            float *x=malloc(sizeof(float)*T*in), *w=malloc(sizeof(float)*in*out*K);
            float *a=malloc(sizeof(float)*n), *b=malloc(sizeof(float)*n), bias[3];
            assert(x&&w&&a&&b);
            for (int i=0;i<T*in;i++) x[i]=value();
            for (int i=0;i<in*out*K;i++) w[i]=value();
            for (int i=0;i<out;i++) bias[i]=value();
            for (size_t i=0;i<n;i++) a[i]=b[i]=value();
            reference_conv(a,x,w,bias,in,out,T,K,dilations[di],accumulate);
            glass_speech_conv1d(b,x,w,bias,in,out,T,K,dilations[di],accumulate);
            assert(!memcmp(a,b,n*sizeof(float)));
            free(x);free(w);free(a);free(b);cases++;
          }
  printf("SPEECH_CONV_PARITY_OK: %u bit-exact cases including padding, dilation, tile tails and accumulation\n",cases);
  return 0;
}
