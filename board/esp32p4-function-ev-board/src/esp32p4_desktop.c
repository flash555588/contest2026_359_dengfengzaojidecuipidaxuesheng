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

#if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
extern int ets_printf(const char *fmt, ...);
#  define desktop_progress(...) ets_printf(__VA_ARGS__)
#else
#  define desktop_progress(...)
#endif

static lv_obj_t *g_clock_label;
static lv_obj_t *g_touch_label;

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

static void touch_event_cb(lv_event_t *e)
{
  lv_indev_t *indev = lv_indev_active();
  lv_point_t point;

  if (indev == NULL || g_touch_label == NULL)
    {
      return;
    }

  lv_indev_get_point(indev, &point);
  lv_label_set_text_fmt(g_touch_label, "touch x=%ld y=%ld",
                        (long)point.x, (long)point.y);
  printf("TOUCH: pressed x=%ld y=%ld\n", (long)point.x, (long)point.y);
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

  if (res.indev == NULL)
    {
      printf("DESKTOP: touch init failed\n");
    }
  else
    {
      printf("DESKTOP: touch ready\n");
    }

  /* Keep the desktop deliberately bright during panel bring-up.  A dark
   * background is indistinguishable from a stopped pixel stream when only
   * the LCD backlight remains on. */

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xDDE8F0), 0);

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
  lv_obj_set_style_text_color(g_clock_label, lv_color_hex(0x102030), 0);
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

  g_touch_label = lv_label_create(lv_screen_active());
  lv_label_set_text(g_touch_label, "touch: tap any point");
  lv_obj_set_style_text_color(g_touch_label, lv_color_hex(0x102030), 0);
  lv_obj_align(g_touch_label, LV_ALIGN_CENTER, 160, 120);

  if (res.indev != NULL)
    {
      lv_indev_add_event_cb(res.indev, touch_event_cb, LV_EVENT_PRESSED,
                            NULL);
    }

  lv_timer_create(clock_timer_cb, 1000, NULL);
  printf("DESKTOP: ui ready\n");
  return 0;
}

static int desktop_task(int argc, FAR char *argv[])
{
  unsigned int loops = 0;

  desktop_progress("T0\n");
  /* Leave the driver's full-screen color bars visible briefly.  This makes
   * it possible to distinguish the continuous scanout path from the first
   * LVGL framebuffer update on physical hardware. */

  printf("DESKTOP: color bars hold for 3s\n");
  nxsig_usleep(3 * 1000 * 1000);
  desktop_progress("T1\n");

  if (desktop_ui() != 0)
    {
      return EXIT_FAILURE;
    }

  desktop_progress("T2\n");

  while (1)
    {
      if (loops < 4)
        {
          desktop_progress("T3 loop=%u\n", loops);
        }

      uint32_t next = lv_timer_handler();

      if (loops < 4)
        {
          desktop_progress("T4 loop=%u next=%lu\n", loops,
                           (unsigned long)next);
        }

      loops++;
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
  /* Keep the UI below NSH/init priority so a display fault cannot starve
   * the shell while the panel path is still being qualified on v3.x. */

  int pid = task_create("desktop", 80, 32768, desktop_task, NULL);
  printf("DESKTOP: task -> %d\n", pid);
  return pid < 0 ? -1 : OK;
#else
  return OK;
#endif
}
