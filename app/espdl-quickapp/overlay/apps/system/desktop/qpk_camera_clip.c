/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "qpk_camera_clip.h"
#include <nuttx/video/uvc_camera.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static uint64_t clip_now(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
void qpk_camera_clip_free(struct qpk_camera_clip *clip)
{if(clip){for(unsigned i=0;i<QPK_CLIP_FRAMES;i++)free(clip->frames[i].jpeg);memset(clip,0,sizeof(*clip));}}

int qpk_camera_clip_capture(struct qpk_camera_clip *clip,atomic_bool *cancel)
{
  if(!clip||!cancel)return -EINVAL;
  memset(clip,0,sizeof(*clip));
  if(atomic_load(cancel))return -ECANCELED;
  int fd=open("/dev/uvcctl",O_RDONLY);if(fd<0)return -errno;
  struct uvc_camera_list list={0};int ret=ioctl(fd,UVCIOC_LIST,(unsigned long)&list),saved=errno;
  close(fd);if(ret<0)return -saved;
  struct uvc_camera_start mode={0};unsigned device=UVC_CAMERA_MAX_DEVICES;
  for(unsigned i=0;i<list.count&&i<UVC_CAMERA_MAX_DEVICES;i++){
    const struct uvc_camera_device *d=&list.devices[i];
    if(d->connected&&d->supported&&d->generation&&d->id<UVC_CAMERA_MAX_DEVICES){
      if(device!=UVC_CAMERA_MAX_DEVICES)return -E2BIG;
      device=d->id;mode.generation=d->generation;
    }
  }
  if(device==UVC_CAMERA_MAX_DEVICES)return -ENODEV;
  char path[24];snprintf(path,sizeof(path),"/dev/uvc%u",device);
  fd=open(path,O_RDONLY|O_NONBLOCK);if(fd<0)return -errno;
  struct uvc_camera_caps *caps=calloc(1,sizeof(*caps));
  unsigned char *input=NULL;bool started=false;ret=-ENOMEM;
  if(!caps)goto done;
  if(ioctl(fd,UVCIOC_CAPS,(unsigned long)caps)<0){ret=-errno;goto done;}
  if(caps->generation!=mode.generation){ret=-ESTALE;goto done;}
  int selected=-1;unsigned area=0;
  for(unsigned i=0;i<caps->count&&i<UVC_CAMERA_MAX_MODES;i++){
    const struct uvc_camera_mode *m=&caps->modes[i];unsigned a=(unsigned)m->width*m->height;
    if(m->width>=160&&m->height>=120&&m->width<=640&&m->height<=480&&
       m->max_frame_size&&m->max_frame_size<=1024u*1024&&m->default_interval&&a>area){selected=i;area=a;}
  }
  if(selected<0){ret=-ENOTSUP;goto done;}
  mode.mode=selected;mode.interval=caps->modes[selected].default_interval;
  if(atomic_load(cancel)){ret=-ECANCELED;goto done;}
  if(ioctl(fd,UVCIOC_START,(unsigned long)&mode)<0){ret=-errno;goto done;}
  started=true;
  if(!mode.max_frame_size||mode.max_frame_size>1024u*1024){ret=-EFBIG;goto done;}
  input=malloc(mode.max_frame_size);if(!input){ret=-ENOMEM;goto done;}
  uint64_t start=clip_now(),first=0,deadline=start+10000;
  ret=-ETIMEDOUT;
  while(clip_now()<deadline){
    if(atomic_load(cancel)){ret=-ECANCELED;break;}
    struct pollfd p={.fd=fd,.events=POLLIN};int ready=poll(&p,1,100);
    if(ready<0){if(errno==EINTR)continue;ret=-errno;break;}
    if(p.revents&(POLLERR|POLLHUP|POLLNVAL)){ret=-ENODEV;break;}
    if(!ready)continue;
    ssize_t n=read(fd,input,mode.max_frame_size);
    if(n<0){if(errno==EINTR||errno==EAGAIN)continue;ret=-errno;break;}
    if(n<4||n>QPK_CLIP_FRAME_LIMIT||input[0]!=0xff||input[1]!=0xd8||input[n-2]!=0xff||input[n-1]!=0xd9)continue;
    uint64_t now=clip_now();
    if(now>=deadline)break;
    if(!first){first=now;deadline=first+5500;}
    uint32_t elapsed=now-first;
    /* Keep draining the live stream; retain only fresh frames at 0/2/4 s. */
    if(clip->count<QPK_CLIP_FRAMES&&elapsed>=clip->count*2000){
      struct qpk_clip_frame *f=&clip->frames[clip->count];
      f->jpeg=malloc(n);if(!f->jpeg){ret=-ENOMEM;break;}
      memcpy(f->jpeg,input,n);f->size=n;f->offset_ms=elapsed;clip->count++;
    }
    if(elapsed>=5000&&clip->count==QPK_CLIP_FRAMES){clip->duration_ms=elapsed;ret=0;break;}
  }
done:
  if(started&&ioctl(fd,UVCIOC_STOP,0)<0&&!ret)ret=-errno;
  if(close(fd)<0&&!ret)ret=-errno;
  free(input);free(caps);
  if(atomic_load(cancel))ret=-ECANCELED;
  if(ret)qpk_camera_clip_free(clip);
  return ret;
}
