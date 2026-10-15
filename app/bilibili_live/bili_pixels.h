/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_PIXELS_H
#define BILI_PIXELS_H
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
struct bili_pixels_layout {
  int w,h,dw,dh,ox,oy;
  int xs[640],ys[360];
};
static inline int bili_clip(int x){return x<0?0:x>255?255:x;}
static inline uint16_t bili_rgb(int y,int vr,int ugvg,int ub)
{
  int base=298*(y-16)+128;
  int r=bili_clip((base+vr)>>8),g=bili_clip((base-ugvg)>>8),b=bili_clip((base+ub)>>8);
  return (r>>3)<<11|(g>>2)<<5|(b>>3);
}
/* Native output is centered/cropped at 1:1; scaled output fits 640x360. */
static int bili_pixels_convert(uint16_t *dst,const uint8_t *const p[3],const int stride[2],
                               int w,int h,bool scale,struct bili_pixels_layout *m,
                               atomic_bool *cancel,unsigned *outw,unsigned *outh)
{
  if(w<2||h<2||w>1280||h>720||!p[0]||!p[1]||!p[2])return -EINVAL;
  if(atomic_load(cancel))return -ECANCELED;
  if(!scale||(w==640&&h==360)){
    int dw=w<1024?w:1024,dh=h<600?h:600;
    int sx=((w-dw)/2)&~1,sy=((h-dh)/2)&~1;
    *outw=dw;*outh=dh;
    for(int y=0;y<dh;y+=2){
      if(atomic_load(cancel))return -ECANCELED;
      const uint8_t *y0=p[0]+(sy+y)*stride[0]+sx;
      const uint8_t *y1=y+1<dh?y0+stride[0]:y0;
      const uint8_t *u=p[1]+(sy+y)/2*stride[1]+sx/2,*v=p[2]+(sy+y)/2*stride[1]+sx/2;
      uint16_t *d0=dst+y*dw,*d1=y+1<dh?d0+dw:d0;
      for(int x=0;x<dw;x+=2){
        int U=u[x/2]-128,V=v[x/2]-128,vr=409*V,ugvg=100*U+208*V,ub=516*U;
        d0[x]=bili_rgb(y0[x],vr,ugvg,ub);
        if(x+1<dw)d0[x+1]=bili_rgb(y0[x+1],vr,ugvg,ub);
        if(y+1<dh){d1[x]=bili_rgb(y1[x],vr,ugvg,ub);if(x+1<dw)d1[x+1]=bili_rgb(y1[x+1],vr,ugvg,ub);}
      }
    }
    return 0;
  }
  *outw=640;*outh=360;
  if(w==1280&&h==720){
    for(int y=0;y<360;y++){
      if(atomic_load(cancel))return -ECANCELED;
      const uint8_t *yp=p[0]+y*2*stride[0],*u=p[1]+y*stride[1],*v=p[2]+y*stride[1];
      for(int x=0;x<640;x++){int U=u[x]-128,V=v[x]-128;dst[y*640+x]=bili_rgb(yp[x*2],409*V,100*U+208*V,516*U);}
    }
    return 0;
  }
  if(m->w!=w||m->h!=h){
    m->w=w;m->h=h;m->dw=640;m->dh=h*640/w;
    if(m->dh>360){m->dh=360;m->dw=w*360/h;}
    if(m->dw<1||m->dh<1)return -EINVAL;
    m->ox=(640-m->dw)/2;m->oy=(360-m->dh)/2;
    for(int x=0;x<m->dw;x++)m->xs[x]=x*w/m->dw;
    for(int y=0;y<m->dh;y++)m->ys[y]=y*h/m->dh;
  }
  if(m->dw!=640||m->dh!=360)memset(dst,0,640*360*2);
  for(int y=0;y<m->dh;y++){
    if(atomic_load(cancel))return -ECANCELED;
    int sy=m->ys[y];const uint8_t *yp=p[0]+sy*stride[0],*u=p[1]+sy/2*stride[1],*v=p[2]+sy/2*stride[1];
    int previous=-1,vr=0,ugvg=0,ub=0;
    for(int x=0;x<m->dw;x++){
      int sx=m->xs[x],chroma=sx/2;
      if(chroma!=previous){int U=u[chroma]-128,V=v[chroma]-128;vr=409*V;ugvg=100*U+208*V;ub=516*U;previous=chroma;}
      dst[(y+m->oy)*640+x+m->ox]=bili_rgb(yp[sx],vr,ugvg,ub);
    }
  }
  return 0;
}
#endif
