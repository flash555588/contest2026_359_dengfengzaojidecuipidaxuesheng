/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#define CAMERA_ASPECT_COUNT 4
struct camera_reference_rect {unsigned x,y,w,h;};
static const unsigned camera_aspects[5][2]={{0,0},{16,10},{16,9},{4,3},{1,1}};
static inline unsigned camera_aspect_height(unsigned width,unsigned aspect)
{return width*camera_aspects[aspect][1]/camera_aspects[aspect][0];}
static inline struct camera_reference_rect camera_frame_content(unsigned w,unsigned h,unsigned iw,unsigned ih)
{
  if(!iw||!ih)return (struct camera_reference_rect){0,0,w,h};
  unsigned dw=w,dh=(uint64_t)ih*w/iw;
  if(dh>h){dh=h;dw=(uint64_t)iw*h/ih;}
  if(!dw)dw=1;
  if(!dh)dh=1;
  return (struct camera_reference_rect){(w-dw)/2,(h-dh)/2,dw,dh};
}
/* Aspect zero retains the original canvas; explicit ratios crop image pixels. */
static inline struct camera_reference_rect camera_frame_crop(unsigned w,unsigned h,unsigned iw,unsigned ih,unsigned zoom,unsigned aspect)
{
  struct camera_reference_rect r=aspect?camera_frame_content(w,h,iw,ih):
    (struct camera_reference_rect){0,0,w,h};
  unsigned cw=r.w,ch=r.h;
  if(aspect){
    unsigned rw=camera_aspects[aspect][0],rh=camera_aspects[aspect][1];
    if((uint64_t)cw*rh>(uint64_t)ch*rw)cw=(uint64_t)ch*rw/rh;
    else ch=(uint64_t)cw*rh/rw;
  }
  cw=cw*100/zoom;ch=ch*100/zoom;
  if(!cw)cw=1;
  if(!ch)ch=1;
  return (struct camera_reference_rect){r.x+(r.w-cw)/2,r.y+(r.h-ch)/2,cw,ch};
}
