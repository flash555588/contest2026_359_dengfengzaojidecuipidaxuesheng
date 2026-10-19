/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_camera_avi.h"
#include "qpk_camera_scale.h"
#include "camera_color.h"
#include "qpk_hardware.h"
#include "qpk_volume.h"
#include <sys/statvfs.h>

#define QPK_VIDEO_W 320
#define QPK_VIDEO_H 180
#define QPK_VIDEO_FPS 10
#define QPK_VIDEO_RATE 16000
#define QPK_VIDEO_BYTES (QPK_VIDEO_W*QPK_VIDEO_H*2)
#ifndef QPK_VIDEO_MOUNT
#define QPK_VIDEO_MOUNT "/sdcard"
#endif
#ifndef QPK_VIDEO_DIR
#define QPK_VIDEO_DIR QPK_VIDEO_MOUNT "/videos"
#endif
struct qpk_video_job {
  struct qpk_hw_progress progress;
  struct qpk_avi avi;
  bool ready,producing,have_last;
  uint64_t started,next_frame;
  unsigned slot_state[3],slot_sequence[3];
  uint16_t frames[3][QPK_VIDEO_W*QPK_VIDEO_H],last[QPK_VIDEO_W*QPK_VIDEO_H];
  char path[128];
};
static pthread_mutex_t g_video_lock=PTHREAD_MUTEX_INITIALIZER;
static struct qpk_video_job *g_video_job;
static bool g_video_authorized;
static struct {bool video;int status;unsigned elapsed_ms,dropped,revision;char path[128];} g_video;
static void qpk_video_authorize(bool allowed)
{
  pthread_mutex_lock(&g_video_lock);g_video_authorized=allowed;
  if(!allowed){g_video.video=false;if(g_video_job)atomic_store(&g_video_job->progress.finish,true);}
  pthread_mutex_unlock(&g_video_lock);
}
static bool qpk_video_allowed(void)
{pthread_mutex_lock(&g_video_lock);bool allowed=g_video_authorized;pthread_mutex_unlock(&g_video_lock);return allowed;}

static int qpk_video_space(void)
{
  int ret=qpk_volume_mounted(QPK_VOLUME_SD);if(ret)return ret;
  struct statvfs fs;if(statvfs(QPK_VIDEO_MOUNT,&fs))return -errno;
  return (uint64_t)fs.f_bavail*fs.f_frsize>=8*1024*1024?0:-ENOSPC;
}
static void qpk_video_stop(void)
{
  pthread_mutex_lock(&g_video_lock);
  if(g_video_job){atomic_store(&g_video_job->progress.finish,true);g_video.status=3;}
  pthread_mutex_unlock(&g_video_lock);
}
static bool qpk_video_mode(bool video)
{
  pthread_mutex_lock(&g_video_lock);bool ok=!g_video_job&&(!video||g_video_authorized);
  if(ok)g_video.video=video;
  pthread_mutex_unlock(&g_video_lock);return ok;
}
static bool qpk_video_selected(void)
{pthread_mutex_lock(&g_video_lock);bool video=g_video_authorized&&g_video.video;pthread_mutex_unlock(&g_video_lock);return video;}
static int qpk_video_status(unsigned *elapsed)
{pthread_mutex_lock(&g_video_lock);int status=g_video.status;if(elapsed)*elapsed=g_video.elapsed_ms;pthread_mutex_unlock(&g_video_lock);return status;}

/* Capture only copies into a reserved slot. Storage and codec waits never run
 * here; a detached writer owns its copies and can finalize after preview exits. */
static void qpk_video_frame(const uint16_t *source,unsigned w,unsigned h,unsigned stride)
{
  if(pthread_mutex_trylock(&g_video_lock))return;
  struct qpk_video_job *j=g_video_job;uint64_t now=qpk_now_ms();int slot=-1;
  if(j&&j->ready&&!atomic_load(&j->progress.finish)&&now>=j->next_frame){
    j->next_frame=now+1000/QPK_VIDEO_FPS;
    for(unsigned i=0;i<3;i++)if(!j->slot_state[i]){slot=i;break;}
    if(slot>=0){j->slot_state[slot]=1;j->producing=true;j->slot_sequence[slot]=(now-j->started)*QPK_VIDEO_FPS/1000;}
    else g_video.dropped++;
  }
  pthread_mutex_unlock(&g_video_lock);if(slot<0)return;
  unsigned zoom=qpk_camera_zoom_percent();
  unsigned crop_w=w*100/zoom,crop_h=h*100/zoom;
  qpk_video_scale(j->frames[slot],source+((h-crop_h)/2)*stride+(w-crop_w)/2,
                  crop_w,crop_h,stride);
  unsigned style=qpk_camera_style();
  if(style)for(unsigned i=0;i<QPK_VIDEO_W*QPK_VIDEO_H;i++)j->frames[slot][i]=camera_color_pixel(j->frames[slot][i],style);
  pthread_mutex_lock(&g_video_lock);j->slot_state[slot]=j->ready?2:0;j->producing=false;pthread_mutex_unlock(&g_video_lock);
}
static bool qpk_video_take_frame(struct qpk_video_job *j,unsigned sequence)
{
  int chosen=-1;
  pthread_mutex_lock(&g_video_lock);
  for(unsigned i=0;i<3;i++)if(j->slot_state[i]==2&&j->slot_sequence[i]<=sequence&&
      (chosen<0||j->slot_sequence[i]>j->slot_sequence[chosen]))chosen=i;
  if(chosen<0&&!j->have_last)for(unsigned i=0;i<3;i++)if(j->slot_state[i]==2&&
      (chosen<0||j->slot_sequence[i]<j->slot_sequence[chosen]))chosen=i;
  if(chosen>=0){
    j->slot_state[chosen]=3;
    for(unsigned i=0;i<3;i++)if(j->slot_state[i]==2&&j->slot_sequence[i]<=j->slot_sequence[chosen])j->slot_state[i]=0;
  }
  pthread_mutex_unlock(&g_video_lock);
  if(chosen<0)return j->have_last;
  memcpy(j->last,j->frames[chosen],QPK_VIDEO_BYTES);j->have_last=true;
  pthread_mutex_lock(&g_video_lock);j->slot_state[chosen]=0;pthread_mutex_unlock(&g_video_lock);return true;
}
static int qpk_video_pcm(void *cookie,const uint8_t *pcm,size_t bytes)
{
  struct qpk_video_job *j=cookie;
  pthread_mutex_lock(&g_video_lock);
  if(!j->started){j->started=qpk_now_ms();j->next_frame=j->started;j->ready=true;g_video.status=2;}
  pthread_mutex_unlock(&g_video_lock);
  int ret=qpk_avi_chunk(&j->avi,pcm,bytes,false);
  if(ret==-EFBIG){atomic_store(&j->progress.finish,true);return 0;}if(ret)return ret;
  unsigned wanted=(uint64_t)(j->avi.audio_bytes/2)*QPK_VIDEO_FPS/QPK_VIDEO_RATE;
  while(j->avi.frames<wanted&&qpk_video_take_frame(j,j->avi.frames)){
    ret=qpk_avi_chunk(&j->avi,j->last,QPK_VIDEO_BYTES,true);if(ret)break;
  }
  pthread_mutex_lock(&g_video_lock);g_video.elapsed_ms=(uint64_t)j->avi.audio_bytes*1000/(QPK_VIDEO_RATE*2);pthread_mutex_unlock(&g_video_lock);
  if(ret==-EFBIG){atomic_store(&j->progress.finish,true);return 0;}
  if(!ret&&j->avi.count%32==0&&qpk_video_space())atomic_store(&j->progress.finish,true);
  return ret;
}
static void *qpk_video_worker(void *argument)
{
  struct qpk_video_job *j=argument;FILE *file=NULL;bool created=false;int fd=-1,ret=qpk_video_space();
  if(!ret&&mkdir(QPK_VIDEO_DIR,0700)&&errno!=EEXIST)ret=-errno;
  struct stat st;if(!ret&&(lstat(QPK_VIDEO_DIR,&st)||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode)))ret=-EPERM;
  for(unsigned i=0;!ret&&i<1000;i++){
    snprintf(j->path,sizeof(j->path),QPK_VIDEO_DIR "/VID_%08lx_%03u.avi",(unsigned long)qpk_now_ms(),i);
    fd=open(j->path,O_CREAT|O_EXCL|O_RDWR|O_NOFOLLOW,0600);
    if(fd>=0){created=true;break;}if(errno!=EEXIST)ret=-errno;
  }
  if(!ret&&fd<0)ret=-EEXIST;
  if(!ret){file=fdopen(fd,"w+b");if(!file){ret=-errno;close(fd);fd=-1;}}
  if(!ret)ret=qpk_avi_begin(&j->avi,file,QPK_VIDEO_W,QPK_VIDEO_H,QPK_VIDEO_FPS,QPK_VIDEO_RATE);
  if(!ret)ret=qpk_audio_capture_pcm(qpk_video_pcm,j,&j->progress);
  pthread_mutex_lock(&g_video_lock);j->ready=false;g_video.status=3;pthread_mutex_unlock(&g_video_lock);
  for(;;){pthread_mutex_lock(&g_video_lock);bool producing=j->producing;pthread_mutex_unlock(&g_video_lock);if(!producing)break;usleep(1000);}
  if(!ret&&(!j->avi.frames||!j->avi.audio_bytes))ret=-ENODATA;
  if(!ret)ret=qpk_avi_finish(&j->avi);
  if(!ret&&fsync(fd))ret=-errno;
  if(file&&fclose(file)&&!ret)ret=-errno;
  free(j->avi.index);
  if(ret&&created)unlink(j->path);
  pthread_mutex_lock(&g_video_lock);
  g_video.status=ret?ret:4;if(!ret)snprintf(g_video.path,sizeof(g_video.path),"%s",j->path);
  unsigned elapsed=g_video.elapsed_ms;
  g_video_job=NULL;pthread_mutex_unlock(&g_video_lock);
  printf("[qpk] camera video status=%d frames=%u audio_bytes=%u elapsed_ms=%u\n",ret?ret:4,j->avi.frames,j->avi.audio_bytes,elapsed);
  free(j);return NULL;
}
static int qpk_video_start(void)
{
  if(!qpk_video_allowed())return -EPERM;
  pthread_mutex_lock(&g_video_lock);bool busy=g_video_job!=NULL;pthread_mutex_unlock(&g_video_lock);if(busy)return -EBUSY;
  struct qpk_video_job *j=calloc(1,sizeof(*j));if(!j)return -ENOMEM;
  atomic_init(&j->progress.cancel,false);atomic_init(&j->progress.finish,false);
  atomic_init(&j->progress.bytes,0);atomic_init(&j->progress.elapsed_ms,0);atomic_init(&j->progress.peak,0);
  pthread_mutex_lock(&g_video_lock);
  if(g_video_job||!g_video_authorized){pthread_mutex_unlock(&g_video_lock);free(j);return -EBUSY;}
  g_video_job=j;g_video.status=1;g_video.revision++;g_video.elapsed_ms=g_video.dropped=0;g_video.path[0]=0;pthread_mutex_unlock(&g_video_lock);
  pthread_attr_t attr;pthread_t thread;int ret=pthread_attr_init(&attr);
  if(!ret){
    /* Keep stack use bounded; frame copies and the AVI index are separate
     * user-heap allocations and never live on this worker's stack. */
#ifdef __NuttX__
    ret=pthread_attr_setstacksize(&attr,8192);
#else
    ret=pthread_attr_setstacksize(&attr,24576);
#endif
    if(!ret)ret=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if(!ret)ret=pthread_create(&thread,&attr,qpk_video_worker,j);
    pthread_attr_destroy(&attr);
  }
  if(ret){pthread_mutex_lock(&g_video_lock);g_video_job=NULL;g_video.status=-ret;pthread_mutex_unlock(&g_video_lock);free(j);return -ret;}
  return 0;
}
