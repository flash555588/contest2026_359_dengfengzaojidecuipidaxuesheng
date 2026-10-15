/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
/* Reposition the original eye pixels, retaining every emotion contour.
 * One scratch allocation per player; the reader owns it, never the UI. */
struct grok_gaze_scratch {
  uint8_t mask[128*128/8];
  struct {uint16_t index,color;} dots[2048];
};
static inline bool grok_eye_mask(const struct grok_gaze_scratch *s,unsigned p)
{return (s->mask[p/8]>>(p%8))&1;}
static inline void grok_gaze_frame(uint8_t *pixels,bool black,unsigned emotion,int gaze_x,int gaze_y,struct grok_gaze_scratch *s)
{
  uint16_t *rgb=(uint16_t *)pixels;uint8_t *alpha=pixels+128*128*2;
  unsigned count=0;int x1=128,x2=0,y1=128,y2=0;
  memset(s->mask,0,sizeof(s->mask));
  for(int y=20;y<108;y++)for(int x=20;x<108;x++){
    if((x-64)*(x-64)+(y-64)*(y-64)>44*44)continue;
    unsigned p=y*128+x,v=rgb[p],r=v>>11,g=(v>>5)&63,b=v&31;
    bool eye=black?(r>26&&g>53&&b>26):(r<7&&g<14&&b<7);
    if(alpha[p]<220||!eye)continue;
    if(count==2048)return; /* Unrecognized palette: leave frame intact. */
    s->dots[count].index=p;s->dots[count++].color=v;s->mask[p/8]|=1u<<(p%8);
    if(x<x1)x1=x;if(x>x2)x2=x;if(y<y1)y1=y;if(y>y2)y2=y;
  }
  if(!count)return; /* Blink: do not invent open eyes. */
  if(gaze_x<-1000)gaze_x=-1000;if(gaze_x>1000)gaze_x=1000;
  if(gaze_y<-1000)gaze_y=-1000;if(gaze_y>1000)gaze_y=1000;
  int dx=64-(x1+x2)/2+gaze_x*8/1000,dy=61-(y1+y2)/2+gaze_y*6/1000;
  /* Fill only the old eye pixels from the nearest body pixel on that row. */
  for(unsigned i=0;i<count;i++){
    unsigned p=s->dots[i].index;int x=p%128,y=p/128;
    for(int distance=1;distance<96;distance++){
      int a=x-distance,b=x+distance;
      if(a>=16&&alpha[y*128+a]>=220&&!grok_eye_mask(s,y*128+a)){rgb[p]=rgb[y*128+a];break;}
      if(b<112&&alpha[y*128+b]>=220&&!grok_eye_mask(s,y*128+b)){rgb[p]=rgb[y*128+b];break;}
    }
  }
  if(emotion==2){
    /* The upstream idle contour is baked in perspective. A neutral frontal
     * pose needs equal-size, upright eyes, not just a translated side view.
     * Keep the source blink height and palette; gaze moves both equally. */
    int height=y2-y1+1;if(height>16)height=16;if(height<2)height=2;
    int cx=64+gaze_x*8/1000,cy=61+gaze_y*6/1000;
    for(int eye=-1;eye<=1;eye+=2)for(int row=0;row<height;row++){
      int inset=height>6&&(row==0||row==height-1)?2:height>6&&(row==1||row==height-2)?1:0;
      for(int col=inset;col<6-inset;col++){
        int x=cx+eye*10-3+col,y=cy-height/2+row;
        if(alpha[y*128+x]>=220)rgb[y*128+x]=s->dots[0].color;
      }
    }
    return;
  }
  for(unsigned i=0;i<count;i++){
    int p=s->dots[i].index,x=p%128+dx,y=p/128+dy;
    if(x>=16&&x<112&&y>=16&&y<112&&alpha[y*128+x]>=220)rgb[y*128+x]=s->dots[i].color;
  }
}
