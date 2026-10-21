/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

#define CAMERA_COLOR_STYLE_COUNT 6
/* RGB565 styles: original, vivid, monochrome, warm, cool, vintage.
 * No frame allocation or LUT. */
static inline uint16_t camera_color_pixel(uint16_t pixel,unsigned style)
{
  if(!style||style>=CAMERA_COLOR_STYLE_COUNT)return pixel;
  int r=pixel>>11,g=(pixel>>5)&63,b=pixel&31;
  r=(r<<1)|(r>>4);b=(b<<1)|(b>>4);
  int y=(r*77+g*150+b*29)>>8;
  if(style==2)return (uint16_t)((y>>1)<<11|y<<5|y>>1);
  if(style==1){r=y+(r-y)*5/4;g=y+(g-y)*5/4;b=y+(b-y)*5/4;}
  else if(style==3){r+=6;g+=2;b-=4;}
  else if(style==4){r-=4;g+=1;b+=7;}
  else {r=12+y*3/4;g=6+y*3/4;b=4+y*2/3;}
  r=r<0?0:r>63?63:r;g=g<0?0:g>63?63:g;b=b<0?0:b>63?63:b;
  return (uint16_t)((r>>1)<<11|g<<5|b>>1);
}
