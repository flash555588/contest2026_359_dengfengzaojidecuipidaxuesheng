/****************************************************************************
 * boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_lcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/video/fb.h>
#include <nuttx/video/mipi_dsi.h>

#include "espressif/esp_gpio.h"
#include "espressif/esp_ldo.h"
#include "espressif/esp_mipi_dsi.h"

#include "esp32p4-function-ev-board.h"

#ifdef CONFIG_ESP32P4_FUNCTION_EV_BOARD_LCD

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
extern bool esp_psram_is_initialized(void);
extern uintptr_t esp_psram_extram_vaddr_start(void);
extern uintptr_t esp_psram_extram_vaddr_end(void);
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ESP32-P4-Function-EV-Board 7-inch 1024x600 EK79007 panel. */

#define LCD_XRES                   1024
#define LCD_YRES                   600
#define LCD_BPP                    16
#define LCD_STRIDE                 (LCD_XRES * LCD_BPP / 8)
#define LCD_FB_SIZE                (LCD_STRIDE * LCD_YRES)
#define LCD_FB_COUNT               3

#define LCD_GPIO_RST               27
#define LCD_GPIO_BL                26

#define LCD_DSI_LANES              2
#define LCD_DSI_RATE_MBPS          1000
#define LCD_DPHY_LDO_CHAN          3
#define LCD_DPHY_LDO_MV            2500
#define LCD_PAGEFLIP_TIMEOUT_MS    100
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
#  define LCD_DPI_CLOCK_MHZ         48
#else
#  define LCD_DPI_CLOCK_MHZ         52
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ek79007_cmd_s
{
  uint8_t cmd;
  uint8_t data;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int lcd_getvideoinfo(FAR struct fb_vtable_s *vtable,
                            FAR struct fb_videoinfo_s *vinfo);
static int lcd_getplaneinfo(FAR struct fb_vtable_s *vtable, int planeno,
                            FAR struct fb_planeinfo_s *pinfo);
#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
static int lcd_pandisplay(FAR struct fb_vtable_s *vtable,
                          FAR struct fb_planeinfo_s *pinfo);
#endif
#ifdef CONFIG_FB_UPDATE
static int lcd_updatearea(FAR struct fb_vtable_s *vtable,
                          FAR const struct fb_area_s *area);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Vendor sequence adapted from Espressif's Apache-2.0 EK79007 driver:
 *
 *   esp-iot-solution commit c8c4726501e13849b785f2cb9ae55ec1266b44dc
 *   components/display/lcd/esp_lcd_ek79007/esp_lcd_ek79007.c
 *
 * Sleep-out and display-on are issued separately after the framebuffer has
 * been bound.
 */

static const struct ek79007_cmd_s g_ek79007_init[] =
{
  {0xb2, 0x10},             /* Pad control: 2-lane MIPI */
  {0x80, 0x8b},
  {0x81, 0x78},
  {0x82, 0x84},
  {0x83, 0x88},
  {0x84, 0xa8},
  {0x85, 0xe3},
  {0x86, 0x88},
};

static const uint16_t g_rainbow[] =
{
  0xf800,                   /* Red */
  0xfd20,                   /* Orange */
  0xffe0,                   /* Yellow */
  0x07e0,                   /* Green */
  0x07ff,                   /* Cyan */
  0x001f,                   /* Blue */
  0xf81f,                   /* Magenta */
};

static struct fb_vtable_s g_lcd_vtable =
{
  .getvideoinfo = lcd_getvideoinfo,
  .getplaneinfo = lcd_getplaneinfo,
#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
  .pandisplay   = lcd_pandisplay,
#endif
#ifdef CONFIG_FB_UPDATE
  .updatearea   = lcd_updatearea,
#endif
};

static struct fb_videoinfo_s g_lcd_video =
{
  .fmt     = FB_FMT_RGB16_565,
  .xres    = LCD_XRES,
  .yres    = LCD_YRES,
  .nplanes = 1,
};

static struct fb_planeinfo_s g_lcd_plane =
{
  .fbmem        = NULL,
  .fblen        = LCD_FB_SIZE,
  .stride       = LCD_STRIDE,
  .display      = 0,
  .bpp          = LCD_BPP,
  .xres_virtual = LCD_XRES,
  .yres_virtual = LCD_YRES,
  .xoffset      = 0,
  .yoffset      = 0,
};

static struct esp_ldo_config_t g_mipi_ldo =
{
  .chan_id    = LCD_DPHY_LDO_CHAN,
  .voltage_mv = LCD_DPHY_LDO_MV,
  .handler    = NULL,
};

static FAR uint16_t *g_lcd_fb;
static bool g_lcd_fb_heap;
static bool g_lcd_ready;
#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
#endif
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
static bool g_lcd_previous_dirty;
static uint16_t g_lcd_previous_y1;
static uint16_t g_lcd_previous_y2;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int lcd_getvideoinfo(FAR struct fb_vtable_s *vtable,
                            FAR struct fb_videoinfo_s *vinfo)
{
  if (vtable != &g_lcd_vtable || vinfo == NULL)
    {
      return -EINVAL;
    }

  memcpy(vinfo, &g_lcd_video, sizeof(*vinfo));
  return OK;
}

static int lcd_getplaneinfo(FAR struct fb_vtable_s *vtable, int planeno,
                            FAR struct fb_planeinfo_s *pinfo)
{
  if (vtable != &g_lcd_vtable || pinfo == NULL || planeno != 0 ||
      g_lcd_plane.fbmem == NULL)
    {
      return -EINVAL;
    }

  memcpy(pinfo, &g_lcd_plane, sizeof(*pinfo));
  return OK;
}

#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
static int lcd_pandisplay(FAR struct fb_vtable_s *vtable,
                          FAR struct fb_planeinfo_s *pinfo)
{
  uintptr_t next_fb;
  int count;
  int ret;

  if (vtable != &g_lcd_vtable || pinfo == NULL)
    {
      return -EINVAL;
    }

  if (pinfo->xoffset != 0 ||
      pinfo->yoffset > LCD_YRES * (LCD_FB_COUNT - 1) ||
      pinfo->yoffset % LCD_YRES != 0)
    {
      return -EINVAL;
    }

  /* Consume superseded pan requests in task context.  The DMA ISR only
   * exchanges a pending address and never calls framebuffer upper-half
   * helpers, which are not guaranteed to be IRAM or ISR safe.
   */

  count = fb_paninfo_count(&g_lcd_vtable, FB_NO_OVERLAY);
  while (count-- > 0)
    {
      fb_remove_paninfo(&g_lcd_vtable, FB_NO_OVERLAY);
    }

  next_fb = (uintptr_t)g_lcd_fb +
            (size_t)pinfo->yoffset * LCD_STRIDE;

  /* Queue the page directly in the DSI driver.  The call returns only after
   * the completed-frame ISR switches DW-GDMA to next_fb, so the camera may
   * safely reuse the previous USERPTR page immediately afterwards.
   */

  ret = esp_mipi_dsi_swap_framebuffer((FAR void *)next_fb,
                                      LCD_PAGEFLIP_TIMEOUT_MS);
  if (ret == OK)
    {
      g_lcd_plane.xoffset = pinfo->xoffset;
      g_lcd_plane.yoffset = pinfo->yoffset;
    }

  return ret;
}

#endif

#ifdef CONFIG_FB_UPDATE
static int lcd_updatearea(FAR struct fb_vtable_s *vtable,
                          FAR const struct fb_area_s *area)
{
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  size_t offset;
  size_t rows;
#else
  size_t offset;
  size_t rows;
#endif

  if (vtable != &g_lcd_vtable || g_lcd_fb == NULL)
    {
      return -EAGAIN;
    }

  if (area == NULL)
    {
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
      return OK;
#else
      return esp_mipi_dsi_flush_framebuffer(g_lcd_fb, LCD_FB_SIZE);
#endif
    }

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  if (area->x >= LCD_XRES || area->y >= g_lcd_plane.yres_virtual)
    {
      return OK;
    }

  if (g_lcd_plane.yres_virtual >= LCD_YRES * 2)
    {
      uint16_t current_y1;
      uint16_t current_y2;
      uint16_t flush_y1;
      uint16_t flush_y2;
      int ret;

      /* DIRECT double buffering copies the previous frame's dirty area into
       * the next page before drawing the current invalid area.  Flush their
       * row union so DMA observes both writes without a full-frame sync.
       */

      current_y1 = area->y % LCD_YRES;
      current_y2 = current_y1 + area->h;
      if (current_y2 > LCD_YRES)
        {
          current_y2 = LCD_YRES;
        }

      flush_y1 = current_y1;
      flush_y2 = current_y2;
      if (g_lcd_previous_dirty)
        {
          if (g_lcd_previous_y1 < flush_y1)
            {
              flush_y1 = g_lcd_previous_y1;
            }

          if (g_lcd_previous_y2 > flush_y2)
            {
              flush_y2 = g_lcd_previous_y2;
            }
        }

      g_lcd_previous_dirty = true;
      g_lcd_previous_y1 = current_y1;
      g_lcd_previous_y2 = current_y2;
      offset = (size_t)flush_y1 * LCD_STRIDE;
      ret = esp_mipi_dsi_flush_framebuffer(
        (FAR uint8_t *)g_lcd_fb + offset,
        (size_t)(flush_y2 - flush_y1) * LCD_STRIDE);
      if (ret == OK)
        {
          ret = esp_mipi_dsi_flush_framebuffer(
            (FAR uint8_t *)g_lcd_fb + LCD_FB_SIZE + offset,
            (size_t)(flush_y2 - flush_y1) * LCD_STRIDE);
        }

      return ret;
    }

  rows = area->h;
  if (rows > g_lcd_plane.yres_virtual - area->y)
    {
      rows = g_lcd_plane.yres_virtual - area->y;
    }

  if (rows == 0 || area->w == 0)
    {
      return OK;
    }

  /* LVGL renders through the cacheable PSRAM mapping.  Write back complete
   * dirty rows before FBIOPAN_DISPLAY queues the page for the next VSync.
   * This keeps CPU rendering fast without exposing a partially rendered
   * frame to the DSI scanout engine.
   */

  offset = (size_t)area->y * LCD_STRIDE;
  return esp_mipi_dsi_flush_framebuffer((FAR uint8_t *)g_lcd_fb + offset,
                                        rows * LCD_STRIDE);
#else
  if (area->x >= LCD_XRES || area->y >= LCD_YRES)
    {
      return OK;
    }

  rows = area->h;
  if (rows > LCD_YRES - area->y)
    {
      rows = LCD_YRES - area->y;
    }

  if (rows == 0 || area->w == 0)
    {
      return OK;
    }

  /* Cache synchronization requires contiguous memory.  Keep the complete
   * scanline update used by supported silicon for a partial-width area.
   */

  offset = (size_t)area->y * LCD_STRIDE;
  return esp_mipi_dsi_flush_framebuffer((FAR uint8_t *)g_lcd_fb + offset,
                                        rows * LCD_STRIDE);
#endif
}
#endif

#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
int esp32p4_lcd_prime_pageflip(void)
{
  int ret;

  ret = esp_mipi_dsi_flush_framebuffer(g_lcd_fb, g_lcd_plane.fblen);
  if (ret == OK)
    {
      g_lcd_previous_dirty = false;
    }

  return ret;
}
#endif

static int lcd_panel_initialize(FAR struct mipi_dsi_host *host,
                                FAR struct mipi_dsi_device **panel)
{
  FAR struct mipi_dsi_device *device;
  size_t i;
  int ret;

  device = mipi_dsi_device_register(host, "ek79007", 0);
  if (device == NULL)
    {
      return -ENODEV;
    }

  device->lanes      = LCD_DSI_LANES;
  device->format     = MIPI_DSI_FMT_RGB565;
  device->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
                       MIPI_DSI_MODE_LPM;
  device->hs_rate    = LCD_DSI_RATE_MBPS * 1000000UL;
  device->lp_rate    = 0;

  ret = mipi_dsi_attach(device);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < sizeof(g_ek79007_init) / sizeof(g_ek79007_init[0]); i++)
    {
      ret = mipi_dsi_dcs_write(device, g_ek79007_init[i].cmd,
                               &g_ek79007_init[i].data, 1);
      if (ret < 0)
        {
          syslog(LOG_ERR, "ERROR: EK79007 DCS 0x%02x failed: %d\n",
                 g_ek79007_init[i].cmd, ret);
          return ret;
        }
    }

  *panel = device;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int esp32p4_lcd_show_rainbow(void)
{
  FAR uint16_t *fb;
  size_t colors = sizeof(g_rainbow) / sizeof(g_rainbow[0]);
  size_t x;
  size_t y;

  if (!g_lcd_ready || g_lcd_fb == NULL)
    {
      return -EAGAIN;
    }

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
#  if defined(CONFIG_SYSTEM_DESKTOP)
  memset(g_lcd_fb, 0, g_lcd_plane.fblen);
  return esp_mipi_dsi_flush_framebuffer(g_lcd_fb, g_lcd_plane.fblen);
#  elif defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
  fb = (FAR uint16_t *)esp_mipi_dsi_noncache_addr(g_lcd_fb);
#  else
  fb = (FAR uint16_t *)esp_mipi_dsi_noncache_addr(g_lcd_fb) +
       LCD_XRES * LCD_YRES;
#  endif
#else
  fb = g_lcd_fb;
#endif

  for (y = 0; y < LCD_YRES; y++)
    {
      for (x = 0; x < LCD_XRES; x++)
        {
          fb[y * LCD_XRES + x] =
              g_rainbow[(x * colors) / LCD_XRES];
        }
    }

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  return OK;
#else
  return esp_mipi_dsi_flush_framebuffer(fb, LCD_FB_SIZE);
#endif
}

int up_fbinitialize(int display)
{
  FAR struct mipi_dsi_host *host;
  FAR struct mipi_dsi_device *panel;
  struct esp_mipi_dsi_bus_config_s bus =
  {
    .num_data_lanes     = LCD_DSI_LANES,
    .lane_bit_rate_mbps = LCD_DSI_RATE_MBPS,
  };

  struct esp_mipi_dsi_dpi_config_s dpi =
  {
    .h_res                 = LCD_XRES,
    .v_res                 = LCD_YRES,
    .hsync_pulse_width     = 10,
    .hsync_back_porch      = 160,
    .hsync_front_porch     = 160,
    .vsync_pulse_width     = 1,
    .vsync_back_porch      = 23,
    .vsync_front_porch     = 12,
    .dpi_clock_freq_mhz    = LCD_DPI_CLOCK_MHZ,
    .virtual_channel       = 0,
    .format                = MIPI_DSI_FMT_RGB565,
  };

  int ret;
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  uintptr_t fb_start;
  uintptr_t psram_start;
  uintptr_t psram_end;
#endif

  if (display != 0)
    {
      return -ENODEV;
    }

  if (g_lcd_ready)
    {
      return OK;
    }

  ret = esp_configgpio(LCD_GPIO_BL, OUTPUT);
  if (ret < 0)
    {
      return ret;
    }

  esp_gpiowrite(LCD_GPIO_BL, false);

  ret = esp_ldo_channel_acquire(&g_mipi_ldo);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: MIPI DPHY LDO failed: %d\n", ret);
      return ret;
    }

  ret = esp_configgpio(LCD_GPIO_RST, OUTPUT);
  if (ret < 0)
    {
      return ret;
    }

  esp_gpiowrite(LCD_GPIO_RST, false);
  up_mdelay(10);
  esp_gpiowrite(LCD_GPIO_RST, true);
  up_mdelay(20);

  ret = esp_mipi_dsi_initialize(&bus);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: MIPI-DSI host failed: %d\n", ret);
      return ret;
    }

  host = esp_mipi_dsi_host_get();
  if (host == NULL)
    {
      return -ENODEV;
    }

  ret = esp_mipi_dsi_configure_dpi(&dpi);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: MIPI DPI config failed: %d\n", ret);
      return ret;
    }

  ret = lcd_panel_initialize(host, &panel);
  if (ret < 0)
    {
      return ret;
    }

  /* Allocate scanout from the PSRAM-backed user heap.  Partial mode keeps
   * one DMA scanout frame; legacy direct mode keeps two contiguous pages for
   * VSync page flipping.
   */

#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
  if (!esp_psram_is_initialized())
    {
      syslog(LOG_ERR, "ERROR: PSRAM user heap is unavailable\n");
      return -ENOMEM;
    }

  g_lcd_plane.fblen = LCD_FB_SIZE * LCD_FB_COUNT;
  g_lcd_plane.yres_virtual = LCD_YRES * LCD_FB_COUNT;
#elif defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  if (!esp_psram_is_initialized())
    {
      syslog(LOG_ERR, "ERROR: PSRAM user heap is unavailable\n");
      return -ENOMEM;
    }
#endif

  g_lcd_fb = memalign(64, g_lcd_plane.fblen);
  if (g_lcd_fb == NULL)
    {
      syslog(LOG_ERR, "ERROR: LCD FB alloc failed (%u bytes)\n",
             (unsigned int)g_lcd_plane.fblen);
      return -ENOMEM;
    }

  g_lcd_fb_heap = true;

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  fb_start = (uintptr_t)g_lcd_fb;
  psram_start = esp_psram_extram_vaddr_start();
  psram_end = esp_psram_extram_vaddr_end();

  if (fb_start < psram_start || fb_start >= psram_end ||
      g_lcd_plane.fblen > psram_end - fb_start)
    {
      syslog(LOG_ERR, "ERROR: LCD FB was not allocated from PSRAM\n");
      free(g_lcd_fb);
      g_lcd_fb = NULL;
      g_lcd_fb_heap = false;
      return -ENOMEM;
    }
#endif

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  g_lcd_plane.fbmem = g_lcd_fb;
#  if defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
  ret = esp_mipi_dsi_bind_framebuffer(g_lcd_fb, LCD_FB_SIZE,
                                      LCD_XRES, LCD_YRES, LCD_BPP);
#  else
  ret = esp_mipi_dsi_bind_framebuffer(
      (FAR uint8_t *)g_lcd_fb + LCD_FB_SIZE, LCD_FB_SIZE,
      LCD_XRES, LCD_YRES, LCD_BPP);
#  endif
#else
  g_lcd_plane.fbmem = g_lcd_fb;
  ret = esp_mipi_dsi_bind_framebuffer(g_lcd_fb, LCD_FB_SIZE,
                                      LCD_XRES, LCD_YRES, LCD_BPP);
#endif
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: LCD FB bind failed: %d\n", ret);
      goto errout_fb;
    }

#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
  g_lcd_plane.yoffset = LCD_YRES;
#endif

#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
  /* FBIOPAN_DISPLAY queues pages directly with the DSI driver. */
#endif

  g_lcd_ready = true;
  ret = esp32p4_lcd_show_rainbow();
  if (ret < 0)
    {
      goto errout_fb;
    }

  ret = mipi_dsi_dcs_exit_sleep_mode(panel);
  if (ret < 0)
    {
      goto errout_fb;
    }

  up_mdelay(120);

  ret = esp_mipi_dsi_video_start();
  if (ret < 0)
    {
      goto errout_fb;
    }

  ret = mipi_dsi_dcs_set_display_on(panel);
  if (ret < 0)
    {
      /* DMA owns the framebuffer after video_start; keep it allocated. */

      g_lcd_ready = false;
      return ret;
    }

  up_mdelay(20);
  esp_gpiowrite(LCD_GPIO_BL, true);

  return OK;

errout_fb:
  g_lcd_ready = false;
  g_lcd_plane.fbmem = NULL;
  if (g_lcd_fb_heap)
    {
      free(g_lcd_fb);
    }

  g_lcd_fb = NULL;
  g_lcd_fb_heap = false;
  return ret;
}

FAR struct fb_vtable_s *up_fbgetvplane(int display, int vplane)
{
  if (display != 0 || vplane != 0 || !g_lcd_ready)
    {
      return NULL;
    }

  return &g_lcd_vtable;
}

void up_fbuninitialize(int display)
{
  UNUSED(display);
}

#endif /* CONFIG_ESP32P4_FUNCTION_EV_BOARD_LCD */
