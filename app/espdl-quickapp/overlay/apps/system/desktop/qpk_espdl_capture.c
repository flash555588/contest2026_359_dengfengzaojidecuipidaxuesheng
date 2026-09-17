/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "qpk_espdl.h"
#include "qpk_mjpeg.h"
#include <nuttx/video/uvc_camera.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void)
{
  struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
  return (uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;
}
struct capture_context { int fd; struct uvc_camera_start mode; uint8_t *input; void *decoder; };
void qpk_dl_capture_close(void *context)
{
  struct capture_context *c=context;
  if (!c) return;
  ioctl(c->fd,UVCIOC_STOP,0);close(c->fd);
  qpk_mjpeg_destroy(c->decoder);free(c->input);free(c);
}
int qpk_dl_capture_open(const struct qpk_dl_request *request,void **context)
{
  *context=NULL;
  char path[24];snprintf(path,sizeof(path),"/dev/uvc%u",request->device);
  int fd=open(path,O_RDONLY);
  if (fd<0) return -errno;
  struct uvc_camera_caps *caps=calloc(1,sizeof(*caps));
  struct uvc_camera_start mode={.generation=request->generation};
  uint8_t *input=NULL;void *decoder=NULL;
  int ret=-ENOMEM;
  bool started=false;
  if (!caps) goto done;
  if (ioctl(fd,UVCIOC_CAPS,(unsigned long)caps)<0) {ret=-errno;goto done;}
  if (caps->generation!=request->generation) {ret=-ESTALE;goto done;}
  int selected=-1;
  for (unsigned i=0;i<caps->count && i<UVC_CAMERA_MAX_MODES;i++) {
    if (caps->modes[i].width==640 && caps->modes[i].height==480) {selected=i;break;}
    if (selected<0 && caps->modes[i].width<=1280 && caps->modes[i].height<=720) selected=i;
  }
  if (selected<0) {ret=-ENOTSUP;goto done;}
  mode.mode=selected;mode.interval=caps->modes[selected].default_interval;
  if (qpk_dl_should_cancel()) {ret=-ECANCELED;goto done;}
  if (ioctl(fd,UVCIOC_START,(unsigned long)&mode)<0) {ret=-errno;goto done;}
  started=true;
  if (!mode.max_frame_size || mode.max_frame_size>UVC_CAMERA_MAX_FRAME) {ret=-EFBIG;goto done;}
  input=malloc(mode.max_frame_size+512);decoder=qpk_mjpeg_create();
  if (!input || !decoder) {ret=-ENOMEM;goto done;}
  struct capture_context *c=malloc(sizeof(*c));
  if (!c) {ret=-ENOMEM;goto done;}
  *c=(struct capture_context){fd,mode,input,decoder};
  *context=c;free(caps);return 0;
done:
  if (started) ioctl(fd,UVCIOC_STOP,0);
  close(fd);qpk_mjpeg_destroy(decoder);free(input);free(caps);
  return ret;
}
int qpk_dl_capture_next(void *context,uint16_t *pixels)
{
  struct capture_context *c=context;
  int fd=c->fd,ret;
  struct uvc_camera_start mode=c->mode;
  uint8_t *input=c->input;void *decoder=c->decoder;
  uint64_t deadline=now_ms()+6000;
  struct pollfd pollfd={.fd=fd,.events=POLLIN};
  ret=-ETIMEDOUT;
  while (now_ms()<deadline) {
    if (qpk_dl_should_cancel()) {ret=-ECANCELED;break;}
    int ready=poll(&pollfd,1,100);
    if (ready<0) {if (errno==EINTR) continue;ret=-errno;break;}
    if (pollfd.revents&(POLLHUP|POLLERR|POLLNVAL)) {ret=-ENODEV;break;}
    if (!ready) continue;
    ssize_t length=read(fd,input,mode.max_frame_size);
    if (length<0) {if (errno==EAGAIN || errno==EINTR) continue;ret=-errno;break;}
    if (!length) continue;
    int decoded=qpk_mjpeg_decode(decoder,input,length,mode.max_frame_size+512,pixels,
                                 QPK_DL_WIDTH,QPK_DL_HEIGHT,mode.width,mode.height);
    if (!decoded) {ret=0;break;}
  }
  return ret;
}
