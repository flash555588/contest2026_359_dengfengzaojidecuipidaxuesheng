/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/video/uvc_camera.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "qpk_camera_clip.h"
static unsigned scenario,stops,closes,starts,reads;
static uint64_t milliseconds;
static atomic_bool cancel;
static int fake_clock(clockid_t id,struct timespec *t)
{assert(id==CLOCK_MONOTONIC);t->tv_sec=milliseconds/1000;t->tv_nsec=milliseconds%1000*1000000;return 0;}
static int fake_open(const char *path,int flags,...)
{(void)flags;assert(!strcmp(path,"/dev/uvcctl")||!strcmp(path,"/dev/uvc0"));return !strcmp(path,"/dev/uvcctl")?10:11;}
static int fake_close(int fd){assert(fd==10||fd==11);closes++;return 0;}
static int fake_ioctl(int fd,unsigned long cmd,...)
{
  unsigned long arg=0;
  if(cmd!=UVCIOC_STOP){va_list ap;va_start(ap,cmd);arg=va_arg(ap,unsigned long);va_end(ap);}
  if(cmd==UVCIOC_LIST){assert(fd==10);struct uvc_camera_list *l=(void *)arg;l->count=scenario==8?2:1;l->devices[0]=(struct uvc_camera_device){.id=0,.connected=1,.supported=1,.generation=9};
    if(scenario==8){l->devices[1]=l->devices[0];l->devices[1].id=1;}}
  else if(cmd==UVCIOC_CAPS){struct uvc_camera_caps *c=(void *)arg;c->generation=scenario==5?10:9;c->count=3;
    c->modes[0]=(struct uvc_camera_mode){.width=320,.height=240,.max_frame_size=153600,.default_interval=333333};
    c->modes[1]=(struct uvc_camera_mode){.width=640,.height=480,.max_frame_size=614400,.default_interval=333333};
    c->modes[2]=(struct uvc_camera_mode){.width=65535,.height=65535,.max_frame_size=614400,.default_interval=333333};}
  else if(cmd==UVCIOC_START){starts++;if(scenario==6){errno=EBUSY;return -1;}struct uvc_camera_start *s=(void *)arg;
    assert(s->generation==9&&s->mode==1);s->width=640;s->height=480;s->max_frame_size=614400;}
  else if(cmd==UVCIOC_STOP){assert(fd==11);stops++;}
  else assert(0);return 0;
}
static int fake_poll(struct pollfd *p,nfds_t n,int timeout)
{
  assert(n==1&&timeout==100);milliseconds+=100;
  if(scenario==1&&milliseconds>2100)atomic_store(&cancel,true);
  if(scenario==2&&milliseconds>2100){p->revents=POLLHUP;return 1;}
  if(scenario==3)return 0;
  if(scenario==7&&milliseconds%300==0){errno=EINTR;return -1;}
  p->revents=POLLIN;return 1;
}
static ssize_t fake_read(int fd,void *dst,size_t n)
{
  assert(fd==11&&n==614400);unsigned char jpeg[]={255,216,(unsigned char)++reads,255,217};
  if(scenario==4)jpeg[0]=0;
  memcpy(dst,jpeg,sizeof(jpeg));return sizeof(jpeg);
}
#define clock_gettime fake_clock
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define poll fake_poll
#define read fake_read
#include "../overlay/apps/system/desktop/qpk_camera_clip.c"
#undef clock_gettime
#undef open
#undef close
#undef ioctl
#undef poll
#undef read
int main(void)
{
  for(scenario=0;scenario<9;scenario++){
    milliseconds=100;stops=closes=starts=reads=0;atomic_store(&cancel,false);
    struct qpk_camera_clip clip={0};int ret=qpk_camera_clip_capture(&clip,&cancel);
    assert(closes==(scenario==8?1:2)&&stops==(scenario==5||scenario==6||scenario==8?0:1));
    if(scenario==0||scenario==7){
      assert(!ret&&clip.count==3&&clip.duration_ms>=5000&&clip.duration_ms<=5500);
      assert(clip.frames[0].offset_ms==0&&clip.frames[1].offset_ms>=2000&&clip.frames[2].offset_ms>=4000);
      assert(clip.frames[0].jpeg!=clip.frames[1].jpeg&&clip.frames[0].jpeg[2]!=clip.frames[1].jpeg[2]);
      assert(clip.frames[0].size==5);qpk_camera_clip_free(&clip);
    }else{assert(ret<0&&!clip.count);for(unsigned i=0;i<3;i++)assert(!clip.frames[i].jpeg);}
    assert(milliseconds<=15600);
  }
  unsigned before=closes;atomic_store(&cancel,true);struct qpk_camera_clip clip;
  assert(qpk_camera_clip_capture(&clip,&cancel)==-ECANCELED&&closes==before);
  puts("CAMERA_CLIP_OK: native JPEG sampling, real temporal spacing, private buffers, cancel/unplug/timeout/malformed/stale/busy/EINTR cleanup, bounded lifetime, no LCD or storage");return 0;
}
