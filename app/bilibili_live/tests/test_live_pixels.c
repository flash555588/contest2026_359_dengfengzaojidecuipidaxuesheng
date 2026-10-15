#include "bili_pixels.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static uint16_t reference(int y,int u,int v){int Y=y-16,U=u-128,V=v-128;int r=bili_clip((298*Y+409*V+128)>>8),g=bili_clip((298*Y-100*U-208*V+128)>>8),b=bili_clip((298*Y+516*U+128)>>8);return (r>>3)<<11|(g>>2)<<5|(b>>3);}
int main(void){
  struct bili_pixels_layout m={0};atomic_bool cancel=false;
  const int sizes[][2]={{640,360},{1280,720},{320,180},{854,480},{640,480},{321,181}};
  uint16_t *dst=malloc(1024*600*2);assert(dst);
  for(unsigned n=0;n<sizeof(sizes)/sizeof(*sizes);n++){
    int w=sizes[n][0],h=sizes[n][1],stride=w+16,uv=(w+1)/2+8;
    uint8_t *y=malloc(stride*h),*u=malloc(uv*((h+1)/2)),*v=malloc(uv*((h+1)/2));assert(y&&u&&v);
    for(int i=0;i<stride*h;i++)y[i]=(i*37+i/stride)%256;
    for(int i=0;i<uv*((h+1)/2);i++){u[i]=(i*13)%256;v[i]=(i*79)%256;}
    const uint8_t *p[3]={y,u,v};int st[2]={stride,uv};
    for(int fit=0;fit<2;fit++)for(int repeat=0;repeat<2;repeat++){
      unsigned ow=0,oh=0;assert(!bili_pixels_convert(dst,p,st,w,h,fit,&m,&cancel,&ow,&oh));
      int dw,dh,ox=0,oy=0,sx0=0,sy0=0;
      if(fit){dw=640;dh=h*640/w;if(dh>360){dh=360;dw=w*360/h;}ox=(640-dw)/2;oy=(360-dh)/2;assert(ow==640&&oh==360);}
      else{dw=w<1024?w:1024;dh=h<600?h:600;sx0=((w-dw)/2)&~1;sy0=((h-dh)/2)&~1;assert(ow==(unsigned)dw&&oh==(unsigned)dh);}
      for(unsigned row=0;row<oh;row++)for(unsigned col=0;col<ow;col++){
        uint16_t expected=0;
        if(!fit||(col>=(unsigned)ox&&col<(unsigned)(ox+dw)&&row>=(unsigned)oy&&row<(unsigned)(oy+dh))){
          int sx=fit?((int)col-ox)*w/dw:sx0+col,sy=fit?((int)row-oy)*h/dh:sy0+row;
          expected=reference(y[sy*stride+sx],u[sy/2*uv+sx/2],v[sy/2*uv+sx/2]);
        }
        assert(dst[row*ow+col]==expected);
      }
    }
    atomic_store(&cancel,true);unsigned ow,oh;assert(bili_pixels_convert(dst,p,st,w,h,1,&m,&cancel,&ow,&oh)==-ECANCELED);atomic_store(&cancel,false);
    free(y);free(u);free(v);
  }
  free(dst);puts("PASS every RGB565 pixel: native crop, odd edges, 640 native, 720 downsample, aspect padding, map cache and cancellation");
}
