/****************************************************************************
 * boards/.../src/esp32p4_desktop.c
 *
 * Minimal LVGL desktop shell for the Function-EV-Board L1 demo:
 * spawns after display/touch init, draws a title bar, an uptime clock
 * and two touchable buttons on the bound framebuffer.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <nuttx/sched.h>
#include <nuttx/signal.h>
#include <nuttx/video/mipi_dsi.h>

#include "lvgl.h"

#define DESKTOP_W  1024
#define DESKTOP_H  600

static lv_obj_t *g_clock_label;

static void clock_timer_cb(lv_timer_t *timer)
{
  uint32_t sec = lv_tick_get() / 1000;
  lv_label_set_text_fmt(g_clock_label, "uptime %lu:%02lu:%02lu",
                        (unsigned long)(sec / 3600),
                        (unsigned long)((sec / 60) % 60),
                        (unsigned long)(sec % 60));
}

static void btn_event_cb(lv_event_t *e)
{
  lv_obj_t *label = lv_event_get_user_data(e);
  static int presses;
  lv_label_set_text_fmt(label, "tapped x%d", ++presses);
}

static int desktop_ui(void)
{
  lv_init();

  lv_nuttx_dsc_t dsc;
  lv_nuttx_result_t res;
  lv_nuttx_dsc_init(&dsc);
  dsc.fb_path    = "/dev/fb0";
  dsc.input_path = "/dev/input0";
  lv_nuttx_init(&dsc, &res);

  if (res.disp == NULL)
    {
      printf("DESKTOP: display init failed\n");
      return -1;
    }

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x102030), 0);

  /* title bar */
  lv_obj_t *bar = lv_obj_create(lv_screen_active());
  lv_obj_set_size(bar, DESKTOP_W, 56);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E88E5), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);

  lv_obj_t *title = lv_label_create(bar);
  lv_label_set_text(title, "openvela \xC2\xB7 ESP32-P4 Desktop");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_center(title);

  /* clock */
  g_clock_label = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_font(g_clock_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(g_clock_label, lv_color_white(), 0);
  lv_obj_align(g_clock_label, LV_ALIGN_CENTER, 0, -40);
  clock_timer_cb(NULL);

  /* buttons */
  lv_obj_t *b1 = lv_btn_create(lv_screen_active());
  lv_obj_set_size(b1, 220, 90);
  lv_obj_align(b1, LV_ALIGN_CENTER, -160, 120);
  lv_obj_t *l1 = lv_label_create(b1);
  lv_label_set_text(l1, "tap me");
  lv_obj_center(l1);

  lv_obj_t *l2 = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_color(l2, lv_color_hex(0x90CAF9), 0);
  lv_obj_align(l2, LV_ALIGN_BOTTOM_MID, 0, -40);

  lv_obj_add_event_cb(b1, btn_event_cb, LV_EVENT_CLICKED, l2);

  lv_timer_create(clock_timer_cb, 1000, NULL);
  printf("DESKTOP: ui ready\n");
  return 0;
}

static int desktop_task(int argc, FAR char *argv[])
{
  if (desktop_ui() != 0)
    {
      return EXIT_FAILURE;
    }

  while (1)
    {
      uint32_t next = lv_timer_handler();
      nxsig_usleep((next < 5 ? 5 : next > 50 ? 50 : next) * 1000);
    }

  return 0;
}

/****************************************************************************
 * Spawned from board bring-up once display + touch are registered.
 ****************************************************************************/

int esp32p4_desktop_start(void)
{
#ifdef CONFIG_GRAPHICS_LVGL
  int pid = task_create("desktop", 100, 32768, desktop_task, NULL);
  printf("DESKTOP: task -> %d\n", pid);
  return pid < 0 ? -1 : OK;
#else
  return OK;
#endif
}
