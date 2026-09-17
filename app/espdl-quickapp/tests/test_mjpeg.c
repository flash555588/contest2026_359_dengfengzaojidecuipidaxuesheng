/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_mjpeg.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  qpk_mjpeg_destroy(decoder);free(jpeg);
  puts("PASS: actual MJPEG decoder, four scales, repeat, output bounds and truncated input");
}
