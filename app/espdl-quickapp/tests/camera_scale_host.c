/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_camera_scale.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static volatile unsigned digest;
__attribute__((noinline,noclone)) static void before(uint16_t *dst,const uint16_t *source,unsigned w,unsigned h,unsigned stride)
{
  unsigned dw=320,dh=(uint64_t)h*dw/w;
  if(dh>180){dh=180;dw=(uint64_t)w*dh/h;}
  unsigned ox=(320-dw)/2,oy=(180-dh)/2;memset(dst,0,320*180*2);
  for(unsigned y=0;y<dh;y++){
    const uint16_t *row=source+(size_t)(y*h/dh)*stride;
    uint16_t *out=dst+(180-1-y-oy)*320+ox;
    for(unsigned x=0;x<dw;x++)out[x]=row[x*w/dw];
  }
}
__attribute__((noinline,noclone)) static void after(uint16_t *dst,const uint16_t *source,unsigned w,unsigned h,unsigned stride)
{qpk_video_scale(dst,source,w,h,stride);}
static double now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec*1e-9;}
int main(void)
{
  unsigned sizes[][2]={{1920,1080},{1280,720},{640,480},{320,180},{180,320},{640,360},{1920,1},{1,1080},{17,13},{321,181},{320,240}};
  uint16_t *a=malloc(320*180*2),*b=malloc(320*180*2);assert(a&&b);
  for(unsigned k=0;k<sizeof(sizes)/sizeof(*sizes);k++)for(unsigned pad=0;pad<2;pad++){
    unsigned w=sizes[k][0],h=sizes[k][1],stride=w+pad*7;
    uint16_t *src=malloc((size_t)stride*h*2);assert(src);
    for(size_t i=0;i<(size_t)stride*h;i++)src[i]=(uint16_t)(i*371+19);
    memset(a,0xa5,320*180*2);memset(b,0x5a,320*180*2);
    before(a,src,w,h,stride);after(b,src,w,h,stride);assert(!memcmp(a,b,320*180*2));free(src);
  }
  after(b,NULL,0,0,0);for(unsigned i=0;i<320*180;i++)assert(!b[i]);
  uint16_t *src=calloc(1280*720,2);assert(src);double times[2];
  for(unsigned mode=0;mode<2;mode++){
    double start=now();
    for(unsigned i=0;i<300;i++){src[0]=(uint16_t)i;(mode?after:before)(b,src,1280,720,1280);digest+=b[i%(320*180)];}
    times[mode]=(now()-start)*1000;
  }
  printf("CAMERA_SCALE_OK: 22 pixel-identical cases; host_ms before=%.3f after=%.3f ratio=%.3f\n",times[0],times[1],times[1]/times[0]);
  free(src);free(a);free(b);
}
