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
  puts("PASS: MJPEG four scales, repeat, bounds, truncated input; read-only still JPEG; missing-DHT camera frames");
}
