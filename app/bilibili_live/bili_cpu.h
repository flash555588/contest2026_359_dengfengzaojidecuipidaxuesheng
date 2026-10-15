/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_CPU_H
#define BILI_CPU_H
#ifdef __NuttX__
#include <nuttx/config.h>
#include <sched.h>
void bili_cpu_register_ui(void);
void bili_cpu_video_begin(void);
void bili_cpu_video_end(void);
void bili_cpu_audio_begin(void);
void bili_cpu_report(void);
unsigned bili_cpu_frame_period_us(void);
int bili_cpu_set_target_fps(unsigned fps);
#else
static inline void bili_cpu_register_ui(void) {}
static inline void bili_cpu_video_begin(void) {}
static inline void bili_cpu_video_end(void) {}
static inline void bili_cpu_audio_begin(void) {}
static inline void bili_cpu_report(void) {}
static inline unsigned bili_cpu_frame_period_us(void) { return 100000; }
static inline int bili_cpu_set_target_fps(unsigned fps) { (void)fps;return 0; }
#endif
static inline int bili_bind_cpu(unsigned cpu)
{
#if defined(__NuttX__) && defined(CONFIG_SMP) && CONFIG_SMP_NCPUS > 1
  cpu_set_t mask;
  if(cpu>=CONFIG_SMP_NCPUS)return -1;
  CPU_ZERO(&mask);CPU_SET(cpu,&mask);
  return sched_setaffinity(0,sizeof(mask),&mask);
#else
  (void)cpu;return 0;
#endif
}
#endif
