/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static atomic_int active_models,active_cameras,fail_stage,hold_stage,entered;
static unsigned frame;
static int gate(int stage) {
  entered=stage;
  while (hold_stage==stage && !qpk_dl_should_cancel()) usleep(1000);
  if (qpk_dl_should_cancel()) return -ECANCELED;
  return fail_stage==stage?-EIO:0;
}
int qpk_dl_backend_open(enum qpk_dl_mode mode,void **ctx) {
  int r=gate(1);if (r) return r;
  *ctx=&active_models;active_models++;return 0;
}
void qpk_dl_backend_close(void *ctx) {if(ctx) active_models--;}
int qpk_dl_backend_verify(void *ctx) {return 0;}
int qpk_dl_capture_open(const struct qpk_dl_request *r,void **ctx) {
  int e=gate(2);if(e) return e;
  *ctx=&active_cameras;active_cameras++;frame=0;return 0;
}
void qpk_dl_capture_close(void *ctx) {if(ctx) active_cameras--;}
int qpk_dl_capture_next(void *ctx,uint16_t *pixels) {
  assert(ctx);int e=gate(3);if(e)return e;
  ++frame;for(unsigned i=0;i<QPK_DL_PIXELS;i++)pixels[i]=frame;
  return 0;
}
int qpk_dl_backend_run(void *ctx,const uint16_t *pixels,struct qpk_dl_result *r) {
  assert(ctx);int e=gate(4);if(e)return e;
  r->count=1;r->items[0]=(struct qpk_dl_item){.score=.9f,.x1=120,.y1=80,.x2=200,.y2=160};
  return 0;
}
static struct qpk_dl_result wait_done(void) {
  struct qpk_dl_result r;
  for(int i=0;i<3000;i++){qpk_dl_status(&r);if(!r.busy){assert(!active_models&&!active_cameras);return r;}usleep(1000);}
  assert(!"worker timeout");return r;
}
static void test_tracker(void) {
  struct qpk_dl_tracker t={0};struct qpk_dl_result r={.count=2};
  r.items[0]=(struct qpk_dl_item){.score=.9f,.x1=30,.y1=60,.x2=100,.y2=140};
  r.items[1]=(struct qpk_dl_item){.score=.9f,.x1=200,.y1=40,.x2=250,.y2=100};
  qpk_dl_track_update(&t,&r);assert(r.track.target==0&&r.track.offset_x<0&&r.track.id==1);
  struct qpk_dl_item tmp=r.items[0];r.items[0]=r.items[1];r.items[1]=tmp;
  qpk_dl_track_update(&t,&r);assert(r.track.target==1&&r.track.id==1);
  r.count=1;
  for(int i=0;i<3;i++){qpk_dl_track_update(&t,&r);assert(r.track.state==QPK_DL_LOST&&r.track.target==-1&&r.track.offset_x==0);}
  qpk_dl_track_update(&t,&r);assert(r.track.state==QPK_DL_TRACKING&&r.track.id==2&&r.track.offset_x>0);
  memset(&t,0,sizeof(t));r.items[0]=(struct qpk_dl_item){.score=.9f,.x1=120,.y1=80,.x2=200,.y2=160};
  qpk_dl_track_update(&t,&r);assert(!r.track.offset_x&&!r.track.offset_y);
  r.items[0].x2=-500;qpk_dl_track_update(&t,&r);assert(r.track.state==QPK_DL_LOST);
}
int main(void) {
  test_tracker();
  struct qpk_dl_request request={.mode=QPK_DL_CLASSIFY,.device=0,.generation=1};
  static uint16_t pixels[QPK_DL_PIXELS];
  assert(qpk_dl_start(NULL)==-EINVAL);
  for(int i=0;i<20;i++) {
    int id=qpk_dl_start(&request);assert(id>0);
    struct qpk_dl_result r=wait_done(),copied;
    assert(!r.error&&r.count==1&&r.frame==1&&r.preview);
    assert(qpk_dl_copy_preview(id,1,pixels,QPK_DL_PIXELS,&copied)==0&&pixels[100]==1);
    assert(qpk_dl_copy_preview(id,2,pixels,QPK_DL_PIXELS,&copied)==-EAGAIN);
    qpk_dl_cancel();assert(qpk_dl_copy_preview(id,1,pixels,QPK_DL_PIXELS,&copied)==-EAGAIN);
  }
  request.mode=QPK_DL_FACE;
  int id=qpk_dl_start(&request);assert(id>0);
  assert(qpk_dl_start(&request)==-EBUSY);
  struct qpk_dl_result r,copied;
  for(int i=0;i<1000;i++) {
    qpk_dl_status(&r);
    if(r.frame>=3) break;
    usleep(1000);
  }
  assert(r.busy&&r.frame>=3&&r.track.state==QPK_DL_TRACKING);
  if(!qpk_dl_copy_preview(id,r.frame,pixels,QPK_DL_PIXELS,&copied))
    for(unsigned i=0;i<QPK_DL_PIXELS;i++)assert(pixels[i]==copied.frame);
  qpk_dl_cancel();r=wait_done();assert(r.error==-ECANCELED&&!r.preview);
  for(int stage=1;stage<=4;stage++) {
    fail_stage=stage;assert(qpk_dl_start(&request)>0);r=wait_done();assert(r.error==-EIO&&!r.count);
    fail_stage=0;hold_stage=stage;entered=0;assert(qpk_dl_start(&request)>0);
    for(int i=0;i<2000&&entered!=stage;i++)usleep(1000);
    assert(entered==stage);qpk_dl_cancel();r=wait_done();assert(r.error==-ECANCELED);hold_stage=0;
  }
  puts("PASS: target association, loss/reacquisition, centering, continuous frames, cancellation and resource release");
}
