/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

typedef void (*mc_fn)(const uint8_t*,int32_t,uint8_t*,int32_t,
                      int16_t,int16_t,int32_t,int32_t);
extern "C" void ref_luma(const uint8_t*,int32_t,uint8_t*,int32_t,int16_t,int16_t,int32_t,int32_t);
extern "C" void opt_luma(const uint8_t*,int32_t,uint8_t*,int32_t,int16_t,int16_t,int32_t,int32_t);
extern "C" void ref_chroma(const uint8_t*,int32_t,uint8_t*,int32_t,int16_t,int16_t,int32_t,int32_t);
extern "C" void opt_chroma(const uint8_t*,int32_t,uint8_t*,int32_t,int16_t,int16_t,int32_t,int32_t);
extern "C" int bili_mc_set_features(unsigned);

static uint8_t src[4096] __attribute__((aligned(16)));
static uint8_t expected[4096] __attribute__((aligned(16)));
static uint8_t actual[4096] __attribute__((aligned(16)));
static unsigned cases;

static void compare(mc_fn ref,mc_fn opt,int fractions)
{
  const int strides[]={32,48},sizes[]={4,8,16},offsets[]={0,1,3,15},dest[]={0,1,8};
  for(int pattern=0;pattern<5;pattern++) {
    uint32_t rng=0x31415926;
    for(unsigned i=0;i<sizeof(src);i++) {
      rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
      src[i]=pattern==0?0:pattern==1?255:pattern==2?i:pattern==3?(i&1?255:0):rng;
    }
    for(int ss:strides)for(int ds:strides)for(int off:offsets)for(int d:dest)
      for(int w:sizes)for(int h:sizes)for(int x=0;x<fractions;x++)for(int y=0;y<fractions;y++) {
        if(!x&&!y&&(d&7))continue; /* Upstream copy requires aligned output. */
        memset(expected,0xa5,sizeof(expected));memset(actual,0xa5,sizeof(actual));
        const uint8_t *input=src+512+off;
        ref(input,ss,expected+64+d,ds,x,y,w,h);
        opt(input,ss,actual+64+d,ds,x,y,w,h);
        if(memcmp(expected,actual,sizeof(actual))) {
          fprintf(stderr,"Mismatch fractions=%d pattern=%d strides=%d/%d offset=%d/%d mv=%d/%d size=%d/%d\n",
                  fractions,pattern,ss,ds,off,d,x,y,w,h);assert(false);
        }
        cases++;
      }
  }
}

int main()
{
  for(unsigned mode=0;mode<4;mode++){
    assert(bili_mc_set_features(mode)>=0);
    compare(ref_luma,opt_luma,4);compare(ref_chroma,opt_chroma,8);
  }
  assert(bili_mc_set_features(4)<0);
  assert(bili_mc_set_features(3)==3);
  /* Fast fused kernels must retain the original temporary-buffer path when
   * source/destination neighborhoods overlap, including unaligned pixels. */
  const int deltas[]={-32,-1,0,1,32};
  for(int delta:deltas)for(int x=0;x<4;x++)for(int y=0;y<4;y++) {
    if(!x&&!y&&(delta&7))continue;
    for(unsigned i=0;i<sizeof(actual);i++)actual[i]=expected[i]=(i*73+i/7)&255;
    ref_luma(expected+512,32,expected+512+delta,32,x,y,8,8);
    opt_luma(actual+512,32,actual+512+delta,32,x,y,8,8);
    assert(!memcmp(expected,actual,sizeof(actual)));cases++;
  }
  printf("PASS: %u differential MC cases, full destination/guard bytes and overlap fallback\n",cases);
}
