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
#include <stdbool.h>

#include <nuttx/sched.h>
#include <nuttx/signal.h>
#include <nuttx/video/mipi_dsi.h>

#include "lvgl.h"

#define DESKTOP_W  1024
#define DESKTOP_H  600
#define TOUCH_ZONE_COUNT 6

#if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
extern int ets_printf(const char *fmt, ...);
#  define desktop_progress(...) ets_printf(__VA_ARGS__)
#else
#  define desktop_progress(...)
#endif

static lv_obj_t *g_clock_label;
static lv_obj_t *g_touch_label;
static lv_obj_t *g_home_screen;
static lv_obj_t *g_touch_test_screen;
static lv_obj_t *g_status_label;
static lv_obj_t *g_coord_label;
static lv_obj_t *g_count_label;
static lv_obj_t *g_trace_label;

typedef struct
{
  lv_obj_t *label;
  int       hits;
} touch_zone_t;

static touch_zone_t g_touch_zones[TOUCH_ZONE_COUNT];
static int g_down_count;
static int g_drag_count;
static int g_up_count;
static bool g_suppress_release;

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

static void touch_test_update_counts(void)
{
  if (g_count_label != NULL)
    {
      lv_label_set_text_fmt(g_count_label,
                            "down %d   drag %d   up %d",
                            g_down_count, g_drag_count, g_up_count);
    }
}

static void touch_indev_cb(lv_event_t *e)
{
  lv_indev_t *indev = lv_indev_active();
  lv_event_code_t code = lv_event_get_code(e);
  lv_point_t point;

  if (indev == NULL)
    {
      return;
    }

  lv_indev_get_point(indev, &point);

  /* Keep the original home-page feedback so the existing bring-up log and
   * manual checks remain unchanged. */

  if (code == LV_EVENT_PRESSED &&
      (g_touch_test_screen == NULL ||
       lv_screen_active() != g_touch_test_screen))
    {
      if (g_touch_label != NULL)
        {
          lv_label_set_text_fmt(g_touch_label, "touch x=%ld y=%ld",
                                (long)point.x, (long)point.y);
        }

      printf("TOUCH: pressed x=%ld y=%ld\n",
             (long)point.x, (long)point.y);
      return;
    }

  if (g_touch_test_screen == NULL ||
      lv_screen_active() != g_touch_test_screen)
    {
      return;
    }

  if (code == LV_EVENT_PRESSED)
    {
      g_down_count++;
      if (g_status_label != NULL)
        {
          lv_label_set_text(g_status_label, "PRESS");
        }

      printf("TOUCHTEST: down x=%ld y=%ld\n",
             (long)point.x, (long)point.y);
    }
  else if (code == LV_EVENT_PRESSING)
    {
      g_drag_count++;
      if (g_status_label != NULL)
        {
          lv_label_set_text(g_status_label, "DRAG");
        }
    }
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
      /* Loading a new screen during the opening button's PRESSED event makes
       * its RELEASED event arrive on the destination screen.  That transport
       * event must not be counted as a panel touch. */

      if (g_suppress_release)
        {
          g_suppress_release = false;
          return;
        }

      g_up_count++;
      if (g_status_label != NULL)
        {
          lv_label_set_text(g_status_label,
                            code == LV_EVENT_RELEASED ? "RELEASE" : "LOST");
        }

      if (g_trace_label != NULL)
        {
          lv_label_set_text_fmt(g_trace_label,
                                "last release: x=%ld y=%ld",
                                (long)point.x, (long)point.y);
        }

      printf("TOUCHTEST: up x=%ld y=%ld\n",
             (long)point.x, (long)point.y);
    }
  else
    {
      return;
    }

  if (g_coord_label != NULL)
    {
      lv_label_set_text_fmt(g_coord_label, "x=%ld  y=%ld",
                            (long)point.x, (long)point.y);
    }

  touch_test_update_counts();
}

static void touch_zone_event_cb(lv_event_t *e)
{
  touch_zone_t *zone = lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *button;

  if (zone == NULL || zone->label == NULL)
    {
      return;
    }

  button = lv_event_get_target(e);
  if (code == LV_EVENT_PRESSED)
    {
      zone->hits++;
      lv_obj_set_style_bg_color(button, lv_color_hex(0x2E7D32), 0);
    }
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
      lv_obj_set_style_bg_color(button, lv_color_hex(0x1976D2), 0);
    }
  else
    {
      return;
    }

  lv_label_set_text_fmt(zone->label, "zone %d\nhits %d",
                        (int)(zone - g_touch_zones + 1), zone->hits);
}

static void touch_test_reset_cb(lv_event_t *e)
{
  int i;

  (void)e;
  g_down_count = 0;
  g_drag_count = 0;
  g_up_count = 0;

  for (i = 0; i < TOUCH_ZONE_COUNT; i++)
    {
      g_touch_zones[i].hits = 0;
      if (g_touch_zones[i].label != NULL)
        {
          lv_label_set_text_fmt(g_touch_zones[i].label,
                                "zone %d\nhits 0", i + 1);
        }
    }

  if (g_status_label != NULL)
    {
      lv_label_set_text(g_status_label, "READY");
    }

  if (g_coord_label != NULL)
    {
      lv_label_set_text(g_coord_label, "x=--  y=--");
    }

  if (g_trace_label != NULL)
    {
      lv_label_set_text(g_trace_label, "last release: --");
    }

  touch_test_update_counts();
}

static void show_home_cb(lv_event_t *e)
{
  (void)e;
  if (g_home_screen != NULL &&
      lv_screen_active() != g_home_screen)
    {
      lv_screen_load(g_home_screen);
    }
}

static void show_touch_test_cb(lv_event_t *e)
{
  (void)e;
  if (g_touch_test_screen != NULL &&
      lv_screen_active() != g_touch_test_screen)
    {
      lv_screen_load(g_touch_test_screen);
      g_suppress_release = true;
    }
}

static void create_touch_test_screen(void)
{
  static const int16_t zone_x[TOUCH_ZONE_COUNT] =
    {
      35, 372, 709, 35, 372, 709
    };

  static const int16_t zone_y[TOUCH_ZONE_COUNT] =
    {
      115, 115, 115, 295, 295, 295
    };

  lv_obj_t *bar;
  lv_obj_t *title;
  lv_obj_t *btn;
  lv_obj_t *label;
  int i;

  g_touch_test_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(g_touch_test_screen,
                            lv_color_hex(0xE8F1FA), 0);
  lv_obj_remove_flag(g_touch_test_screen, LV_OBJ_FLAG_SCROLLABLE);

  bar = lv_obj_create(g_touch_test_screen);
  lv_obj_set_size(bar, DESKTOP_W, 56);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x00695C), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);

  title = lv_label_create(bar);
  lv_label_set_text(title, "Touch Test");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_center(title);

  g_status_label = lv_label_create(g_touch_test_screen);
  lv_label_set_text(g_status_label, "READY");
  lv_obj_set_style_text_color(g_status_label, lv_color_hex(0x102030), 0);
  lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 25, 72);

  g_coord_label = lv_label_create(g_touch_test_screen);
  lv_label_set_text(g_coord_label, "x=--  y=--");
  lv_obj_set_style_text_color(g_coord_label, lv_color_hex(0x102030), 0);
  lv_obj_align(g_coord_label, LV_ALIGN_TOP_MID, 0, 72);

  g_count_label = lv_label_create(g_touch_test_screen);
  lv_obj_set_style_text_color(g_count_label, lv_color_hex(0x102030), 0);
  lv_obj_align(g_count_label, LV_ALIGN_TOP_RIGHT, -25, 72);
  touch_test_update_counts();

  for (i = 0; i < TOUCH_ZONE_COUNT; i++)
    {
      btn = lv_btn_create(g_touch_test_screen);
      lv_obj_set_size(btn, 280, 155);
      lv_obj_align(btn, LV_ALIGN_TOP_LEFT, zone_x[i], zone_y[i]);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x1976D2), 0);

      label = lv_label_create(btn);
      lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
      lv_obj_center(label);

      g_touch_zones[i].label = label;
      g_touch_zones[i].hits = 0;
      lv_label_set_text_fmt(label, "zone %d\nhits 0", i + 1);
      lv_obj_add_event_cb(btn, touch_zone_event_cb, LV_EVENT_PRESSED,
                          &g_touch_zones[i]);
      lv_obj_add_event_cb(btn, touch_zone_event_cb, LV_EVENT_RELEASED,
                          &g_touch_zones[i]);
      lv_obj_add_event_cb(btn, touch_zone_event_cb, LV_EVENT_PRESS_LOST,
                          &g_touch_zones[i]);
    }

  btn = lv_btn_create(g_touch_test_screen);
  lv_obj_set_size(btn, 165, 68);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 25, -18);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x455A64), 0);
  label = lv_label_create(btn);
  lv_label_set_text(label, "home");
  lv_obj_center(label);
  lv_obj_add_event_cb(btn, show_home_cb, LV_EVENT_PRESSED, NULL);

  btn = lv_btn_create(g_touch_test_screen);
  lv_obj_set_size(btn, 175, 68);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 215, -18);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFB8C00), 0);
  label = lv_label_create(btn);
  lv_label_set_text(label, "clear");
  lv_obj_center(label);
  lv_obj_add_event_cb(btn, touch_test_reset_cb, LV_EVENT_CLICKED, NULL);

  g_trace_label = lv_label_create(g_touch_test_screen);
  lv_label_set_text(g_trace_label, "last release: --");
  lv_obj_set_style_text_color(g_trace_label, lv_color_hex(0x405060), 0);
  lv_obj_align(g_trace_label, LV_ALIGN_BOTTOM_RIGHT, -25, -38);

  label = lv_label_create(g_touch_test_screen);
  lv_label_set_text(label,
                    "Tap each zone, then drag across the panel.");
  lv_obj_set_style_text_color(label, lv_color_hex(0x506070), 0);
  lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, -25, -12);
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
  g_home_screen = lv_screen_active();

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

  b1 = lv_btn_create(lv_screen_active());
  lv_obj_set_size(b1, 220, 90);
  lv_obj_align(b1, LV_ALIGN_CENTER, 0, 120);
  l1 = lv_label_create(b1);
  lv_label_set_text(l1, "touch test");
  lv_obj_center(l1);
  create_touch_test_screen();
  lv_obj_add_event_cb(b1, show_touch_test_cb, LV_EVENT_PRESSED, NULL);

  g_touch_label = lv_label_create(lv_screen_active());
  lv_label_set_text(g_touch_label, "touch: tap any point");
  lv_obj_set_style_text_color(g_touch_label, lv_color_hex(0x102030), 0);
  lv_obj_align(g_touch_label, LV_ALIGN_CENTER, 160, 120);

  if (res.indev != NULL)
    {
      lv_indev_add_event_cb(res.indev, touch_indev_cb, LV_EVENT_PRESSED,
                            NULL);
      lv_indev_add_event_cb(res.indev, touch_indev_cb, LV_EVENT_PRESSING,
                            NULL);
      lv_indev_add_event_cb(res.indev, touch_indev_cb, LV_EVENT_RELEASED,
                            NULL);
      lv_indev_add_event_cb(res.indev, touch_indev_cb,
                            LV_EVENT_PRESS_LOST, NULL);
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
