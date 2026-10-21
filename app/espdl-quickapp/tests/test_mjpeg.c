/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_mjpeg.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int main(int argc,char **argv)
{
  assert(argc==2);
  FILE *file=fopen(argv[1],"rb");assert(file);
  fseek(file,0,SEEK_END);size_t size=ftell(file);rewind(file);
  uint8_t *jpeg=malloc(size+512);assert(jpeg);
  assert(fread(jpeg,1,size,file)==size);fclose(file);
  void *decoder=qpk_mjpeg_create();assert(decoder);
  for(unsigned scale=0;scale<4;scale++) {
    unsigned width=320>>scale,height=240>>scale;
    size_t count=(size_t)width*height;
    uint16_t *pixels=malloc((count+16)*2);assert(pixels);
    for(unsigned i=0;i<16;i++)pixels[count+i]=0xa55a;
    for(unsigned repeat=0;repeat<3;repeat++) {
      assert(qpk_mjpeg_decode(decoder,jpeg,size,size+512,pixels,width,height,320,240)==0);
      for(unsigned i=0;i<16;i++)assert(pixels[count+i]==0xa55a);
    }
    for(size_t cut=0;cut<size;cut+=379)
      assert(qpk_mjpeg_decode(decoder,jpeg,cut,size+512,pixels,width,height,320,240)<0);
    free(pixels);
  }
  /* A landscape preview shares each framebuffer row with a retained rail. */
  for(unsigned mode=0;mode<3;mode++) {
    unsigned width=mode==2?640:mode?896:320,height=mode==2?480:mode?600:240,stride=1024;
    uint16_t *pixels=malloc(((size_t)stride*height+16)*2);assert(pixels);
    for(size_t i=0;i<(size_t)stride*height+16;i++)pixels[i]=0xa55a;
    assert(qpk_mjpeg_decode_region(decoder,jpeg,size,size+512,pixels,width,height,stride,320,240)==0);
    for(unsigned y=0;y<height;y++)for(unsigned x=width;x<stride;x++)assert(pixels[y*stride+x]==0xa55a);
    for(unsigned i=0;i<16;i++)assert(pixels[stride*height+i]==0xa55a);
    assert(qpk_mjpeg_decode_region(decoder,jpeg,size,size+512,pixels,width,height,width-1,320,240)<0);
    free(pixels);
  }
  uint8_t *read_only=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  assert(read_only!=MAP_FAILED);memcpy(read_only,jpeg,size);assert(mprotect(read_only,size,PROT_READ)==0);
  size_t count=320*240;
  uint16_t *still=malloc((count+16)*2),*reference=malloc(count*2);assert(still&&reference);
  assert(qpk_mjpeg_decode(decoder,jpeg,size,size+512,reference,320,240,320,240)==0);
  /* Compare the optimized row/column map with the original sampling formula,
   * including black bars, repeated decoder reuse and untouched rail pixels. */
  const unsigned fits[][2]={{896,600},{640,480},{800,600},{320,300},{1000,240}};
  for (unsigned repeat=0;repeat<3;repeat++) for(unsigned mode=0;mode<5;mode++) {
    unsigned w=fits[mode][0],h=fits[mode][1],stride=1024;
    uint16_t *fit=malloc(((size_t)stride*h+16)*2);assert(fit);
    for(size_t i=0;i<(size_t)stride*h+16;i++)fit[i]=0xa55a;
    assert(qpk_mjpeg_decode_region(decoder,jpeg,size,size+512,fit,w,h,stride,320,240)==0);
    unsigned dw=w,dh=240*w/320;
    if(dh>h){dh=h;dw=320*h/240;}
    unsigned ox=(w-dw)/2,oy=(h-dh)/2;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
      uint16_t expected=0;
      if(x>=ox&&x<ox+dw&&y>=oy&&y<oy+dh)
        expected=reference[((y-oy)*240/dh)*320+(x-ox)*320/dw];
      assert(fit[y*stride+x]==expected);
    }
    for(unsigned y=0;y<h;y++)for(unsigned x=w;x<stride;x++)assert(fit[y*stride+x]==0xa55a);
    for(unsigned i=0;i<16;i++)assert(fit[stride*h+i]==0xa55a);
    unsigned vw=w*2/3,vh=h*2/3;
    uint16_t *view=malloc(((size_t)stride*vh+32)*2);assert(view);
    for(size_t i=0;i<(size_t)stride*vh+32;i++)view[i]=0xa55a;
    assert(qpk_mjpeg_decode_view(decoder,jpeg,size,size+512,view+11,w,h,stride,320,240,vw,vh)==0);
    for(unsigned y=0;y<vh;y++)for(unsigned x=0;x<vw;x++)
      assert(view[11+y*stride+x]==fit[(y*h/vh)*stride+x*w/vw]);
    for(unsigned i=0;i<11;i++)assert(view[i]==0xa55a);
    for(unsigned y=0;y<vh;y++)for(unsigned x=vw;x<stride;x++)assert(view[11+y*stride+x]==0xa55a);
    for(unsigned i=11;i<32;i++)assert(view[stride*vh+i]==0xa55a);
    assert(qpk_mjpeg_decode_view(decoder,jpeg,size,size+512,view,w,h,stride,320,240,w+1,vh)<0);
    assert(qpk_mjpeg_decode_view(decoder,jpeg,size/2,size+512,view,w,h,stride,320,240,vw,vh)<0);
    free(view);
    free(fit);
  }
  for(unsigned scale=0;scale<4;scale++) {
    unsigned w=320>>scale,h=240>>scale,stride=320,vw=w*2/3,vh=h*2/3;
    uint16_t *canvas=calloc((size_t)stride*h,2),*view=calloc((size_t)stride*vh,2);
    assert(canvas&&view);
    assert(qpk_mjpeg_decode_region(decoder,jpeg,size,size+512,canvas,w,h,stride,320,240)==0);
    assert(qpk_mjpeg_decode_view(decoder,jpeg,size,size+512,view,w,h,stride,320,240,vw,vh)==0);
    for(unsigned y=0;y<vh;y++)for(unsigned x=0;x<vw;x++)
      assert(view[y*stride+x]==canvas[(y*h/vh)*stride+x*w/vw]);
    free(canvas);free(view);
  }
  uint16_t *quick=malloc(count*2);assert(quick);
  assert(qpk_mjpeg_preview_limits(decoder,160,0)<0);
  for(unsigned scale=1;scale<4;scale++) {
    assert(qpk_mjpeg_preview_limits(decoder,320>>scale,240>>scale)==0);
    assert(qpk_mjpeg_decode_view(decoder,jpeg,size,size+512,quick,320,240,320,320,240,320,240)==0);
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)
      assert(quick[y*320+x]==reference[((y>>scale)<<scale)*320+((x>>scale)<<scale)]);
    /* Preview quality settings must never alter full-resolution photos. */
    assert(qpk_mjpeg_decode_still(decoder,read_only,size,quick,320,240)==0);
    assert(!memcmp(quick,reference,count*2));
  }
  assert(qpk_mjpeg_preview_limits(decoder,0,0)==0);free(quick);
  for(unsigned i=0;i<16;i++)still[count+i]=0xa55a;
  assert(qpk_mjpeg_decode_still(decoder,read_only,size,still,320,240)==0);
  assert(!memcmp(still,reference,count*2));
  for(unsigned i=0;i<16;i++)assert(still[count+i]==0xa55a);
  uint16_t *expanded=malloc((count*4+16)*2);assert(expanded);
  for(unsigned i=0;i<16;i++)expanded[count*4+i]=0xa55a;
  for(unsigned repeat=0;repeat<3;repeat++) {
    assert(qpk_mjpeg_decode_still_fit(decoder,read_only,size,expanded,640,480)==0);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)assert(expanded[y*640+x]==reference[(y/2)*320+x/2]);
    for(unsigned i=0;i<16;i++)assert(expanded[count*4+i]==0xa55a);
  }
  assert(qpk_mjpeg_decode_still_fit(decoder,read_only,size,expanded,640,479)<0);
  assert(qpk_mjpeg_decode_still_fit(decoder,read_only,size,expanded,160,120)<0);
  for(size_t cut=0;cut<size;cut+=379)assert(qpk_mjpeg_decode_still_fit(decoder,read_only,cut,expanded,640,480)<0);
  free(expanded);
  assert(qpk_mjpeg_decode_still(decoder,read_only,size,still,319,240)<0);
  for(size_t cut=0;cut<size;cut+=379)assert(qpk_mjpeg_decode_still(decoder,read_only,cut,still,320,240)<0);
  /* Camera frames without DHT still use the mutable MJPEG entry point. */
  uint8_t *uvc=malloc(size+512);assert(uvc);size_t pos=2,used=2;
  memcpy(uvc,jpeg,2);
  while(pos+4<=size){
    assert(jpeg[pos]==0xff);
    if(jpeg[pos+1]==0xda){memcpy(uvc+used,jpeg+pos,size-pos);used+=size-pos;break;}
    size_t segment=2+((size_t)jpeg[pos+2]<<8)+jpeg[pos+3];assert(pos+segment<=size);
    if(jpeg[pos+1]!=0xc4){memcpy(uvc+used,jpeg+pos,segment);used+=segment;}
    pos+=segment;
  }
  assert(used<size);
  assert(qpk_mjpeg_decode_still(decoder,uvc,used,still,320,240)<0);
  assert(qpk_mjpeg_decode(decoder,uvc,used,size+512,still,320,240,320,240)==0);
  assert(!memcmp(still,reference,count*2));
  free(uvc);free(still);free(reference);munmap(read_only,size);
  qpk_mjpeg_destroy(decoder);free(jpeg);
  puts("PASS: MJPEG four scales, repeat, bounds, truncated input; read-only still JPEG; missing-DHT camera frames; exact fused viewport sampling; preview-only reduced RGB conversion without photo changes");
}
