/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
#include <string.h>
#include <stdlib.h>

static int min(int a,int b) { return a<b?a:b; }
static int max(int a,int b) { return a>b?a:b; }
static int area(const struct qpk_dl_item *b) { return (b->x2-b->x1)*(b->y2-b->y1); }

/* Geometric association, not biometric identity. A distant new face cannot
 * take over until three consecutive misses have released the old target. */
void qpk_dl_track_update(struct qpk_dl_tracker *t,struct qpk_dl_result *r)
{
  int selected=-1,best=-1;
  memset(&r->track,0,sizeof(r->track));r->track.target=-1;
  for (unsigned i=0;i<r->count && i<QPK_DL_MAX_RESULTS;i++) {
    struct qpk_dl_item *b=&r->items[i];
    b->x1=max(0,min(QPK_DL_WIDTH-1,b->x1));b->x2=max(0,min(QPK_DL_WIDTH-1,b->x2));
    b->y1=max(0,min(QPK_DL_HEIGHT-1,b->y1));b->y2=max(0,min(QPK_DL_HEIGHT-1,b->y2));
    int a=area(b);
    if (b->x2<=b->x1 || b->y2<=b->y1 || !(b->score>=.4f)) continue;
    int rank=a;
    if (t->locked) {
      int old=area(&t->box);
      if (a*5<old*2 || old*5<a*2) continue;
      int overlap=max(0,min(b->x2,t->box.x2)-max(b->x1,t->box.x1))*
                  max(0,min(b->y2,t->box.y2)-max(b->y1,t->box.y1));
      int iou=overlap*1000/(a+old-overlap);
      int dx=(b->x1+b->x2-t->box.x1-t->box.x2)/2;
      int dy=(b->y1+b->y2-t->box.y1-t->box.y2)/2;
      int radius=max(24,max(t->box.x2-t->box.x1,t->box.y2-t->box.y1)/2);
      if (iou<100 && dx*dx+dy*dy>radius*radius) continue;
      rank=iou*1000-dx*dx-dy*dy;
    }
    if (selected<0 || rank>best) {best=rank;selected=i;}
  }
  if (selected<0) {
    r->track.id=t->id;
    r->track.state=t->locked?QPK_DL_LOST:QPK_DL_SEARCHING;
    if (t->locked && ++t->missed>=3) t->locked=false;
    return;
  }
  const struct qpk_dl_item *b=&r->items[selected];
  int cx=(b->x1+b->x2)/2,cy=(b->y1+b->y2)/2;
  if (!t->locked) {
    if (++t->id==0) ++t->id;
    t->center_x=cx;t->center_y=cy;
  } else {
    t->center_x=(t->center_x*3+cx*2)/5;t->center_y=(t->center_y*3+cy*2)/5;
  }
  t->box=*b;t->locked=true;t->missed=0;
  r->track.state=QPK_DL_TRACKING;r->track.target=selected;r->track.id=t->id;
  r->track.offset_x=(t->center_x-QPK_DL_WIDTH/2)*2000/QPK_DL_WIDTH;
  r->track.offset_y=(t->center_y-QPK_DL_HEIGHT/2)*2000/QPK_DL_HEIGHT;
  if (abs(r->track.offset_x)<80) r->track.offset_x=0;
  if (abs(r->track.offset_y)<80) r->track.offset_y=0;
}
