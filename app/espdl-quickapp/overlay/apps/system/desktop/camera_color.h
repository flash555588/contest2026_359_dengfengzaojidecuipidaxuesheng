/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

/* RGB565 styles: original, vivid, monochrome. No frame allocation or LUT. */
static inline uint16_t camera_color_pixel(uint16_t pixel,unsigned style)
{
  if(!style)return pixel;
  int r=pixel>>11,g=(pixel>>5)&63,b=pixel&31;
  r=(r<<1)|(r>>4);b=(b<<1)|(b>>4);
  int y=(r*77+g*150+b*29)>>8;
  if(style==2)return (uint16_t)((y>>1)<<11|y<<5|y>>1);
  r=y+(r-y)*5/4;g=y+(g-y)*5/4;b=y+(b-y)*5/4;
  r=r<0?0:r>63?63:r;g=g<0?0:g>63?63:g;b=b<0?0:b>63?63:b;
  return (uint16_t)((r>>1)<<11|g<<5|b>>1);
}
