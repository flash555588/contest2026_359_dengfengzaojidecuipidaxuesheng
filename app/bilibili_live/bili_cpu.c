/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_cpu.h"
#ifdef __NuttX__
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdatomic.h>
#include <errno.h>
static atomic_uint target_fps=10;
unsigned bili_cpu_frame_period_us(void)
{
  return 1000000u/atomic_load(&target_fps);
}
int bili_cpu_set_target_fps(unsigned fps)
{
  if(fps!=5&&fps!=10&&fps!=15&&fps!=20)return -EINVAL;
  atomic_store(&target_fps,fps);
  printf("[media-cpu] late_frame_target=%u fps\n",fps);
  return 0;
}
#if defined(CONFIG_SMP) && CONFIG_SMP_NCPUS > 1
static pthread_mutex_t cpu_lock=PTHREAD_MUTEX_INITIALIZER;
static pid_t ui_pid;
static cpu_set_t ui_saved;
static unsigned video_workers;
void bili_cpu_audio_begin(void)
{
  struct sched_param param={.sched_priority=120};
  int bound=bili_bind_cpu(0);
  int ret=sched_setscheduler(0,SCHED_RR,&param);
  printf("[media-cpu] audio cpu=0 priority=120 affinity=%d scheduler=%d\n",bound,ret);
}
void bili_cpu_register_ui(void)
{
  pthread_mutex_lock(&cpu_lock);
  pid_t pid=getpid();cpu_set_t mask;
  if(sched_getaffinity(pid,sizeof(mask),&mask)==0){ui_pid=pid;ui_saved=mask;}
  pthread_mutex_unlock(&cpu_lock);
}
void bili_cpu_video_begin(void)
{
  pthread_mutex_lock(&cpu_lock);
  if(!video_workers++&&ui_pid>0){
    cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(0,&mask);
    if(sched_setaffinity(ui_pid,sizeof(mask),&mask)<0)fprintf(stderr,"media: UI affinity failed\n");
  }
  pthread_mutex_unlock(&cpu_lock);
  if(bili_bind_cpu(1)<0)fprintf(stderr,"media: video affinity failed\n");
}
void bili_cpu_video_end(void)
{
  pthread_mutex_lock(&cpu_lock);
  if(video_workers&&!--video_workers&&ui_pid>0)
    if(sched_setaffinity(ui_pid,sizeof(ui_saved),&ui_saved)<0)fprintf(stderr,"media: UI affinity restore failed\n");
  pthread_mutex_unlock(&cpu_lock);
}
void bili_cpu_report(void)
{
  pthread_mutex_lock(&cpu_lock);cpu_set_t mask=0;
  int ret=ui_pid>0?sched_getaffinity(ui_pid,sizeof(mask),&mask):-1;
  printf("[media-cpu] ui_mask=%lx workers=%u ret=%d\n",(unsigned long)mask,video_workers,ret);
  printf("[media-cpu] l2_config=%u KiB line_config=%u bytes\n",
    (unsigned)(CONFIG_ESPRESSIF_CACHE_L2_CACHE_SIZE/1024),
    (unsigned)CONFIG_ESPRESSIF_CACHE_L2_CACHE_LINE_SIZE);
  printf("[media-cpu] late_frame_target=%u fps\n",atomic_load(&target_fps));
  pthread_mutex_unlock(&cpu_lock);
}
#else
void bili_cpu_register_ui(void) {}
void bili_cpu_video_begin(void) {}
void bili_cpu_video_end(void) {}
void bili_cpu_audio_begin(void) {}
void bili_cpu_report(void) {}
#endif
#endif
