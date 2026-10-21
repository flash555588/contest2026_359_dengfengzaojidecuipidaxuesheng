/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/spinlock.h>
#include <nuttx/video/camera_timing.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_timer.h"

static spinlock_t timing_lock=SP_UNLOCKED;
static struct esp_camera_timing timing;
static uint64_t last_frame,last_log;
uint64_t IRAM_ATTR esp_camera_timing_now(void){return esp_timer_get_time();}
void IRAM_ATTR esp_camera_display_frame(void)
{
  uint64_t now=esp_camera_timing_now();
  irqstate_t flags=spin_lock_irqsave(&timing_lock);
  if(timing.display_frames){
    uint64_t gap=now-last_frame;
    uint32_t bounded=gap>UINT32_MAX?UINT32_MAX:(uint32_t)gap;
    if(bounded>timing.display_gap_max_us)timing.display_gap_max_us=bounded;
  }
  timing.display_frames++;last_frame=now;
  spin_unlock_irqrestore(&timing_lock,flags);
}
void esp_camera_timing_pan(uint64_t started,int error)
{
  uint64_t elapsed=esp_camera_timing_now()-started;
  uint32_t bounded=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
  irqstate_t flags=spin_lock_irqsave(&timing_lock);
  timing.pan_count++;if(error)timing.pan_errors++;
  if(bounded>timing.pan_max_us)timing.pan_max_us=bounded;
  spin_unlock_irqrestore(&timing_lock,flags);
}
void IRAM_ATTR esp_camera_display_guard_frame(void)
{
  irqstate_t flags=spin_lock_irqsave(&timing_lock);
  timing.display_guard_frames++;
  spin_unlock_irqrestore(&timing_lock,flags);
}
uint64_t esp_camera_flash_begin(unsigned op)
{
  (void)op;
  uint64_t now=esp_camera_timing_now();
  irqstate_t flags=spin_lock_irqsave(&timing_lock);timing.flash_active++;
  spin_unlock_irqrestore(&timing_lock,flags);return now;
}
void esp_camera_flash_end(unsigned op,uint32_t bytes,uint64_t started,int result)
{
  int saved_errno=errno;
  uint64_t now=esp_camera_timing_now(),elapsed=now-started;
  bool log=false;
  irqstate_t flags=spin_lock_irqsave(&timing_lock);
  if(timing.flash_active)timing.flash_active--;
  if(op<3){
    timing.flash_ops[op]++;timing.flash_total_us[op]+=elapsed;
    uint32_t bounded=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
    if(bounded>timing.flash_max_us[op])timing.flash_max_us[op]=bounded;
  }
  if(elapsed>=20000 && now-last_log>=1000000){log=true;last_log=now;}
  spin_unlock_irqrestore(&timing_lock,flags);
  /* Task context only, at most one slow-operation message per second. */
  if(log)printf("[flash-time] op=%u bytes=%lu us=%llu result=%d\n",op,
                (unsigned long)bytes,(unsigned long long)elapsed,result);
  errno=saved_errno;
}
void esp_camera_timing_read(struct esp_camera_timing *out)
{
  irqstate_t flags=spin_lock_irqsave(&timing_lock);*out=timing;
  spin_unlock_irqrestore(&timing_lock,flags);
}
