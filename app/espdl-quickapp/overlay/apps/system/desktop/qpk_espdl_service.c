/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "qpk_espdl.h"
#include <nuttx/video/uvc_camera.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t g_dl_lock=PTHREAD_MUTEX_INITIALIZER;
static struct qpk_dl_result g_dl_result;
static bool g_dl_cancel;
static uint16_t *g_dl_preview;
static uint64_t now_ms(void)
{
  struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
  return (uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;
}
bool qpk_dl_should_cancel(void)
{
  pthread_mutex_lock(&g_dl_lock);bool cancel=g_dl_cancel;pthread_mutex_unlock(&g_dl_lock);
  return cancel;
}
void qpk_dl_set_stage(enum qpk_dl_stage stage)
{
  pthread_mutex_lock(&g_dl_lock);
  if (g_dl_result.busy) g_dl_result.stage=stage;
  pthread_mutex_unlock(&g_dl_lock);
}
void qpk_dl_status(struct qpk_dl_result *result)
{
  pthread_mutex_lock(&g_dl_lock);*result=g_dl_result;pthread_mutex_unlock(&g_dl_lock);
}
void qpk_dl_cancel(void)
{
  pthread_mutex_lock(&g_dl_lock);
  g_dl_cancel=true;
  if (!g_dl_result.busy) {
    free(g_dl_preview);g_dl_preview=NULL;
    g_dl_result.preview=false;g_dl_result.count=0;g_dl_result.stage=QPK_DL_IDLE;
    memset(&g_dl_result.track,0,sizeof(g_dl_result.track));g_dl_result.track.target=-1;
  }
  pthread_mutex_unlock(&g_dl_lock);
}
int qpk_dl_copy_preview(uint32_t request,uint32_t frame,uint16_t *pixels,
                        size_t count,struct qpk_dl_result *result)
{
  if (!pixels || !result || count<QPK_DL_PIXELS) return -EINVAL;
  pthread_mutex_lock(&g_dl_lock);
  int ret=-EAGAIN;
  if (g_dl_result.preview && g_dl_result.request==request &&
      g_dl_result.frame==frame && g_dl_preview) {
    memcpy(pixels,g_dl_preview,QPK_DL_PIXELS*sizeof(uint16_t));
    *result=g_dl_result;ret=0;
  }
  pthread_mutex_unlock(&g_dl_lock);
  return ret;
}
static void *worker(void *arg)
{
  struct qpk_dl_request *request=arg;
  struct qpk_dl_result result;
  struct qpk_dl_tracker tracker={0};
  qpk_dl_status(&result);
  void *backend=NULL,*capture=NULL;
  uint16_t *pixels=malloc(QPK_DL_PIXELS*sizeof(uint16_t));
  int ret=pixels?0:-ENOMEM;
  /* Load once per session. Release all worker resources before clearing busy. */
  if (!ret && !qpk_dl_should_cancel() && !request->selftest) ret=qpk_dl_backend_open(request->mode,&backend);
  if (!ret && !qpk_dl_should_cancel() && request->selftest) ret=qpk_dl_backend_verify(backend);
  if (!ret && !qpk_dl_should_cancel() && !request->selftest) ret=qpk_dl_capture_open(request,&capture);
  uint64_t measured_start=now_ms(),capture_ms=0,infer_ms=0,publish_ms=0;
  unsigned measured_frames=0;
  while (!request->selftest && !ret && !qpk_dl_should_cancel()) {
    uint64_t start=now_ms();
    qpk_dl_set_stage(QPK_DL_CAPTURE);
    ret=qpk_dl_capture_next(capture,pixels);
    if (ret || qpk_dl_should_cancel()) break;
    uint64_t captured=now_ms();
    result.count=0;
    ret=qpk_dl_backend_run(backend,pixels,&result);
    if (ret) break;
    if (request->mode==QPK_DL_FACE) qpk_dl_track_update(&tracker,&result);
    result.elapsed_ms=(uint32_t)(now_ms()-start);
    uint64_t inferred=now_ms();
    result.frame++;result.preview=true;result.stage=QPK_DL_DONE;
    pthread_mutex_lock(&g_dl_lock);
    if (!g_dl_cancel) {
      memcpy(g_dl_preview,pixels,QPK_DL_PIXELS*sizeof(uint16_t));
      g_dl_result=result;
    }
    pthread_mutex_unlock(&g_dl_lock);
    capture_ms+=captured-start;infer_ms+=inferred-captured;publish_ms+=now_ms()-inferred;
    if(++measured_frames==30 || request->mode==QPK_DL_CLASSIFY) {
      unsigned duration=(unsigned)(now_ms()-measured_start);
      printf("[espdl] perf frames=%u wall=%u capture=%u infer=%u publish=%u faces=%u\n",
        measured_frames,duration,(unsigned)(capture_ms/measured_frames),
        (unsigned)(infer_ms/measured_frames),(unsigned)(publish_ms/measured_frames),result.count);
      measured_start=now_ms();capture_ms=infer_ms=publish_ms=0;measured_frames=0;
    }
    if (request->mode==QPK_DL_CLASSIFY) break;
    /* Budget a complete frame at 10 fps; slow frames need no extra delay. */
    uint64_t used=now_ms()-start;
    if (used<100) {
      struct timespec pause={0,(long)(100-used)*1000000};nanosleep(&pause,NULL);
    }
  }
  qpk_dl_capture_close(capture);qpk_dl_backend_close(backend);
  free(pixels);free(request);
  pthread_mutex_lock(&g_dl_lock);
  if (g_dl_cancel) ret=-ECANCELED;
  g_dl_result.error=ret;g_dl_result.busy=false;
  g_dl_result.stage=ret==-ECANCELED?QPK_DL_CANCELLED:ret?QPK_DL_ERROR:QPK_DL_DONE;
  if (ret) {
    g_dl_result.count=0;g_dl_result.preview=false;
    memset(&g_dl_result.track,0,sizeof(g_dl_result.track));g_dl_result.track.target=-1;
    free(g_dl_preview);g_dl_preview=NULL;
  }
  pthread_mutex_unlock(&g_dl_lock);
  return NULL;
}
int qpk_dl_start(const struct qpk_dl_request *request)
{
  if (!request || request->mode<QPK_DL_CLASSIFY || request->mode>QPK_DL_FACE ||
      (!request->selftest && (request->device>=UVC_CAMERA_MAX_DEVICES || !request->generation))) return -EINVAL;
  struct qpk_dl_request *copy=malloc(sizeof(*copy));
  uint16_t *preview=malloc(QPK_DL_PIXELS*sizeof(uint16_t));
  if (!copy || !preview) {free(copy);free(preview);return -ENOMEM;}
  *copy=*request;
  pthread_mutex_lock(&g_dl_lock);
  if (g_dl_result.busy) {pthread_mutex_unlock(&g_dl_lock);free(copy);free(preview);return -EBUSY;}
  uint32_t id=(g_dl_result.request+1)&0x7fffffff;
  if (!id) id=1;
  free(g_dl_preview);g_dl_preview=preview;
  memset(&g_dl_result,0,sizeof(g_dl_result));
  g_dl_result.request=id;g_dl_result.mode=request->mode;g_dl_result.track.target=-1;
  g_dl_result.stage=QPK_DL_LOAD;g_dl_result.busy=true;g_dl_cancel=false;
  pthread_attr_t attr;pthread_t thread;
  int ret=pthread_attr_init(&attr);
  if (!ret) {
    ret=pthread_attr_setstacksize(&attr,32768);
    if (!ret) ret=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if (!ret) ret=pthread_create(&thread,&attr,worker,copy);
    pthread_attr_destroy(&attr);
  }
  if (ret) {
    free(copy);free(g_dl_preview);g_dl_preview=NULL;
    g_dl_result.busy=false;g_dl_result.error=-ret;g_dl_result.stage=QPK_DL_ERROR;
  }
  pthread_mutex_unlock(&g_dl_lock);
  return ret?-ret:(int)id;
}
