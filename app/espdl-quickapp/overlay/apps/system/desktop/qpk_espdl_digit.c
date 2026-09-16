/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
#include <errno.h>
#include <string.h>
int qpk_dl_digit_normalize(const uint8_t *pixels,unsigned width,unsigned height,uint8_t out[784])
{
  if (!pixels || !out || !width || !height || width>512 || height>512) return -EINVAL;
  unsigned left=width,right=0,top=height,bottom=0,total=0;
  for (unsigned y=0;y<height;y++) for (unsigned x=0;x<width;x++) if (pixels[y*width+x]>24) {
    if (x<left) left=x;if (x>right) right=x;
    if (y<top) top=y;if (y>bottom) bottom=y;
    total++;
  }
  memset(out,0,784);
  if (total<8) return -ENODATA;
  unsigned bw=right-left+1,bh=bottom-top+1,largest=bw>bh?bw:bh;
  unsigned dw=(bw*20+largest/2)/largest,dh=(bh*20+largest/2)/largest;
  if (!dw) dw=1;if (!dh) dh=1;
  uint8_t resized[784]={0};
  unsigned ox=(28-dw)/2,oy=(28-dh)/2;
  uint32_t sum=0,mx=0,my=0;
  // Area averaging preserves narrow strokes when shrinking the touch canvas.
  for (unsigned y=0;y<dh;y++) for (unsigned x=0;x<dw;x++) {
    unsigned x0=left+x*bw/dw,x1=left+(x+1)*bw/dw;
    unsigned y0=top+y*bh/dh,y1=top+(y+1)*bh/dh;
    if (x1==x0) x1=x0+1;if (y1==y0) y1=y0+1;
    uint32_t value=0,n=0;
    for (unsigned sy=y0;sy<y1;sy++) for (unsigned sx=x0;sx<x1;sx++) {value+=pixels[sy*width+sx];n++;}
    value=(value+n/2)/n;
    resized[(y+oy)*28+x+ox]=value;
    sum+=value;mx+=value*(x+ox);my+=value*(y+oy);
  }
  if (!sum) return -ENODATA;
  int dx=14-(int)((mx+sum/2)/sum),dy=14-(int)((my+sum/2)/sum);
  for (int y=0;y<28;y++) for (int x=0;x<28;x++) {
    int tx=x+dx,ty=y+dy;
    if (tx>=0 && tx<28 && ty>=0 && ty<28) out[ty*28+tx]=resized[y*28+x];
  }
  return 0;
}
