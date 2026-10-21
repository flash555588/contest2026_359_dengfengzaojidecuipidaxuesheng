/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_mjpeg.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
  fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
  uint8_t *input=malloc(size+512);assert(input);assert(fread(input,1,size,f)==size);fclose(f);
  unsigned w=0,h=0;assert(!qpk_mjpeg_probe(input,size,&w,&h));
  size_t count=(size_t)w*h;uint16_t *full=malloc(count*2),*view=malloc((count+16)*2);assert(full&&view);
  void *decoder=qpk_mjpeg_create();assert(decoder);
  assert(!qpk_mjpeg_decode(decoder,input,size,size+512,full,w,h,w,h));
  for(unsigned scale=1;scale<=3;scale++) {
    unsigned sw=w>>scale,sh=h>>scale;assert(sw&&sh);
    assert(!qpk_mjpeg_preview_limits(decoder,sw,sh));
    for(unsigned i=0;i<16;i++)view[count+i]=0xa55a;
    assert(!qpk_mjpeg_decode_view(decoder,input,size,size+512,view,w,h,w,w,h,w,h));
    unsigned dw=w,dh=sh*w/sw;if(dh>h){dh=h;dw=sw*h/sh;}
    unsigned ox=(w-dw)/2,oy=(h-dh)/2;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
      uint16_t expected=0;
      if(x>=ox&&x<ox+dw&&y>=oy&&y<oy+dh)
        expected=full[(((y-oy)*sh/dh)<<scale)*w+(((x-ox)*sw/dw)<<scale)];
      assert(view[y*w+x]==expected);
    }
    for(unsigned i=0;i<16;i++)assert(view[count+i]==0xa55a);
    assert(qpk_mjpeg_decode_view(decoder,input,size/2,size+512,view,w,h,w,w,h,w,h)<0);
    assert(!qpk_mjpeg_decode_region(decoder,input,size,size+512,view,w,h,w,w,h));
    assert(!memcmp(view,full,count*2));
  }
  qpk_mjpeg_destroy(decoder);free(view);free(full);free(input);
  puts("MJPEG_PREVIEW_OK: reduced RGB sampling, clipped MCU edges, photo isolation and truncation");
}
