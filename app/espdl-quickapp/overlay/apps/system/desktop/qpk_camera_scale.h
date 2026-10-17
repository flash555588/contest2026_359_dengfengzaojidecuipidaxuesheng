/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <string.h>

/* RGB565 AVI frame: preserve nearest-neighbour sampling and bottom-up rows.
 * A bounded row map replaces one integer division per output pixel. */
static void qpk_video_scale(uint16_t *dst,const uint16_t *source,
                            unsigned w,unsigned h,unsigned stride)
{
  enum { WIDTH=320, HEIGHT=180 };
  if(!w||!h||stride<w){memset(dst,0,WIDTH*HEIGHT*2);return;}
  unsigned dw=WIDTH,dh=(uint64_t)h*dw/w;
  if(dh>HEIGHT){dh=HEIGHT;dw=(uint64_t)w*dh/h;}
  unsigned ox=(WIDTH-dw)/2,oy=(HEIGHT-dh)/2;
  if(dw!=WIDTH||dh!=HEIGHT)memset(dst,0,WIDTH*HEIGHT*2);
  if(!dw||!dh)return;
  unsigned columns[WIDTH];
  if(dw!=w)for(unsigned x=0;x<dw;x++)columns[x]=(uint64_t)x*w/dw;
  for(unsigned y=0;y<dh;y++){
    const uint16_t *row=source+(size_t)((uint64_t)y*h/dh)*stride;
    uint16_t *out=dst+(HEIGHT-1-y-oy)*WIDTH+ox;
    if(dw==w)memcpy(out,row,dw*sizeof(*out));
    else for(unsigned x=0;x<dw;x++)out[x]=row[columns[x]];
  }
}
