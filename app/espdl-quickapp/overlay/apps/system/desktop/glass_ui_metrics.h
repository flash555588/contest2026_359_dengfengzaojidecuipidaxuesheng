/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <lvgl/lvgl.h>

/* Display/UI thread only. No sampling timer, per-frame allocation or screenshots.
 * Render time is CPU-side drawing; refresh time also includes display waits. */
static struct {
  uint64_t since,render_start,refresh_start,render_us,refresh_us;
  uint32_t frames,refreshes,render_max_us,refresh_max_us;
  bool rendered;
} g_ui_metrics;
static uint64_t glass_ui_metrics_now(void)
{
  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return 0;
  return (uint64_t)ts.tv_sec*1000000+(uint64_t)ts.tv_nsec/1000;
}
static void glass_ui_metrics_reset(void)
{memset(&g_ui_metrics,0,sizeof(g_ui_metrics));g_ui_metrics.since=glass_ui_metrics_now();}
static void glass_ui_metrics_event(lv_event_t *event)
{
  lv_event_code_t code=lv_event_get_code(event);
  if(code==LV_EVENT_REFR_START){g_ui_metrics.refresh_start=glass_ui_metrics_now();g_ui_metrics.rendered=false;}
  else if(code==LV_EVENT_RENDER_START){g_ui_metrics.render_start=glass_ui_metrics_now();g_ui_metrics.rendered=true;}
  else if(code==LV_EVENT_RENDER_READY&&g_ui_metrics.render_start){
    uint64_t now=glass_ui_metrics_now(),delta=now>=g_ui_metrics.render_start?now-g_ui_metrics.render_start:0;
    g_ui_metrics.render_us+=delta;g_ui_metrics.frames++;
    if(delta>g_ui_metrics.render_max_us)g_ui_metrics.render_max_us=delta>UINT32_MAX?UINT32_MAX:(uint32_t)delta;
    g_ui_metrics.render_start=0;
  }else if(code==LV_EVENT_REFR_READY&&g_ui_metrics.rendered&&g_ui_metrics.refresh_start){
    uint64_t now=glass_ui_metrics_now(),delta=now>=g_ui_metrics.refresh_start?now-g_ui_metrics.refresh_start:0;
    g_ui_metrics.refresh_us+=delta;g_ui_metrics.refreshes++;
    if(delta>g_ui_metrics.refresh_max_us)g_ui_metrics.refresh_max_us=delta>UINT32_MAX?UINT32_MAX:(uint32_t)delta;
    g_ui_metrics.refresh_start=0;g_ui_metrics.rendered=false;
  }
}
static void glass_ui_metrics_attach(lv_display_t *display)
{
  glass_ui_metrics_reset();
  lv_display_add_event_cb(display,glass_ui_metrics_event,LV_EVENT_REFR_START,NULL);
  lv_display_add_event_cb(display,glass_ui_metrics_event,LV_EVENT_REFR_READY,NULL);
  lv_display_add_event_cb(display,glass_ui_metrics_event,LV_EVENT_RENDER_START,NULL);
  lv_display_add_event_cb(display,glass_ui_metrics_event,LV_EVENT_RENDER_READY,NULL);
}
