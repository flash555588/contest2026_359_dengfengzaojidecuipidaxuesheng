/****************************************************************************
 * boards/espressif/esp32p4/esp32p4-function-ev-board/src/esp32p4_display.c
 *
 * Simple Boot display bring-up for the 7" 1024x600 MIPI-DSI panel
 * (EK79007-class driver IC on the LCD adapter board).
 *
 * Sequence: power DPHY LDO -> DSI host init (2 lanes @1Gbps) -> panel
 * hard reset (GPIO27) -> vendor init cmds over LP DCS -> DPI timing ->
 * framebuffer (PSRAM) bind -> video start -> backlight (GPIO26).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/video/mipi_dsi.h>
#include <nuttx/video/fb.h>
#include <nuttx/signal.h>
#include <nuttx/kmalloc.h>

#include "esp_mipi_dsi.h"
#include "esp_ldo.h"
#include "esp_gpio.h"

#define LCD_RST_GPIO       27
#define LCD_BL_GPIO        26

#define FB_W               1024
#define FB_H               600
#define FB_BPP             16
#define FB_BYTES_PER_PIXEL (FB_BPP / 8)
#define FB_SIZE            (FB_W * FB_H * FB_BPP / 8)

#if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
extern int ets_printf(const char *fmt, ...);
#  define lcd_progress(s) ets_printf(s)
#else
#  define lcd_progress(s)
#endif

static FAR uint8_t *g_fb;
static volatile bool g_ready;
static uint32_t g_update_count;

/* EK79007 vendor registers */
#define EK79007_PAD_CONTROL 0xB2
#define EK79007_DSI_2_LANE  0x10
#define EK79007_CMD_SLPOUT  0x11
#define EK79007_CMD_DISPON  0x29
#define EK79007_CMD_COLMOD  0x3a
#define EK79007_COLMOD_16BPP 0x55

static const uint8_t g_ek79007_init[][2] =
{
    {0x80, 0x8B}, {0x81, 0x78}, {0x82, 0x84}, {0x83, 0x88},
    {0x84, 0xA8}, {0x85, 0xE3}, {0x86, 0x88},
};

static int ek79007_dcs_write(FAR struct mipi_dsi_device *dev, uint8_t cmd,
                             FAR const void *data, size_t len)
{
  ssize_t ret;

  ret = mipi_dsi_dcs_write(dev, cmd, data, len);
  if (ret < 0)
    {
      printf("DISP: DCS 0x%02x failed -> %ld\n", cmd, (long)ret);
      return (int)ret;
    }

  return OK;
}

/****************************************************************************
 * Name: esp32p4_display_init
 ****************************************************************************/

int esp32p4_display_init(void)
{
  struct esp_mipi_dsi_bus_config_s bus;
  struct esp_mipi_dsi_dpi_config_s dpi;
  struct mipi_dsi_device *dev;
  struct esp_ldo_config_t phy_ldo;
  uint8_t lane_cmd;
  uint8_t pixel_format;
  uint8_t lane_readback;
  uint8_t power_mode;
  ssize_t read_ret;
  FAR uint8_t *fb = NULL;
  FAR uint16_t *fb16;
  unsigned int i;
  uint32_t dma_frames;
  uint32_t dma_status;
  uint32_t bridge_status;
  uint32_t lane_mbps_x100;
  uint32_t dpi_expect_mhz_x100;
  uint32_t dpi_real_mhz_x100;
  uint32_t host_int_st0;
  uint32_t host_int_st1;
  uint32_t host_vid_pkt_status;
  uint32_t host_phy_status;
  uint32_t host_color_coding;
  struct esp_mipi_dsi_timing_diagnostics_s timing_diag;
  struct esp_mipi_dsi_dma_diagnostics_s dma_diag;
  int ret;

  if (g_ready)
    {
      return OK;
    }

  lcd_progress("L0\n");

  /* A cold-powered panel needs its control pins in a deterministic state
   * before the DPHY and DSI host are enabled.  Keep both the backlight and
   * the active-low reset asserted throughout host initialization. */

  esp_configgpio(LCD_BL_GPIO, OUTPUT);
  esp_gpiowrite(LCD_BL_GPIO, false);
  esp_configgpio(LCD_RST_GPIO, OUTPUT);
  esp_gpiowrite(LCD_RST_GPIO, false);

  /* 1. VDD_MIPI_DPHY from on-chip LDO channel 3 @ 2.5V */

  phy_ldo.chan_id    = 3;
  phy_ldo.voltage_mv = 2500;
  phy_ldo.handler    = NULL;

  ret = esp_ldo_channel_acquire(&phy_ldo);
  printf("DISP: dphy ldo ch3@2.5V -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }

  nxsig_usleep(20 * 1000);

  /* 2. DSI host: match Espressif's EK79007 reference profile exactly.
   * Keeping the lane rate, pixel format, and blanking timings as one
   * coherent profile is important: mixing the old 52 MHz profile with the
   * current 48 MHz profile compresses the generated image horizontally. */

  memset(&bus, 0, sizeof(bus));
  bus.num_data_lanes     = 2;
  bus.lane_bit_rate_mbps = 1000;

  ret = esp_mipi_dsi_initialize(&bus);
  printf("DISP: dsi host init -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }

  dev = mipi_dsi_device_register(esp_mipi_dsi_host_get(), "ek79007", 0);
  if (dev == NULL)
    {
      printf("DISP: device register failed\n");
      return -ENODEV;
    }

  dev->lanes     = 2;
  dev->format    = MIPI_DSI_FMT_RGB565;
  dev->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM;

  ret = mipi_dsi_attach(dev);
  printf("DISP: panel attach -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }

  lcd_progress("L1\n");

  /* 3. Panel hardware reset, active low */

  nxsig_usleep(20 * 1000);
  esp_gpiowrite(LCD_RST_GPIO, true);
  nxsig_usleep(120 * 1000);

  /* 4. Vendor init sequence over LP DCS */

  lane_cmd = EK79007_DSI_2_LANE;
  ret = ek79007_dcs_write(dev, EK79007_PAD_CONTROL, &lane_cmd, 1);
  if (ret != OK)
    {
      return ret;
    }

  for (i = 0; i < sizeof(g_ek79007_init) / sizeof(g_ek79007_init[0]); i++)
    {
      ret = ek79007_dcs_write(dev, g_ek79007_init[i][0],
                              &g_ek79007_init[i][1], 1);
      if (ret != OK)
        {
          return ret;
        }
    }

  /* Revision 1.x has one shared bridge raw pixel type, so use RGB565 from
   * PSRAM through the DSI Host and tell the panel to decode 16 bpp too. */

  pixel_format = EK79007_COLMOD_16BPP;
  ret = ek79007_dcs_write(dev, EK79007_CMD_COLMOD, &pixel_format, 1);
  if (ret != OK)
    {
      return ret;
    }

  ret = ek79007_dcs_write(dev, EK79007_CMD_SLPOUT, NULL, 0);
  if (ret != OK)
    {
      return ret;
    }

  nxsig_usleep(120 * 1000);
  ret = ek79007_dcs_write(dev, EK79007_CMD_DISPON, NULL, 0);
  if (ret != OK)
    {
      return ret;
    }

  nxsig_usleep(20 * 1000);
  lane_readback = 0;
  read_ret = mipi_dsi_dcs_read(dev, EK79007_PAD_CONTROL,
                               &lane_readback, 1);
  printf("DISP: panel B2 read -> %ld value=%02x\n",
         (long)read_ret, lane_readback);

  power_mode = 0;
  read_ret = mipi_dsi_dcs_read(dev, 0x0a, &power_mode, 1);
  printf("DISP: panel power mode -> %ld value=%02x\n",
         (long)read_ret, power_mode);
  printf("DISP: panel init done\n");

  /* 5. DPI timing from the official EK79007 1024x600@60 profile. */

  memset(&dpi, 0, sizeof(dpi));
  dpi.h_res              = FB_W;
  dpi.v_res              = FB_H;
  dpi.hsync_pulse_width  = 10;
  dpi.hsync_back_porch   = 120;
  dpi.hsync_front_porch  = 120;
  dpi.vsync_pulse_width  = 1;
  dpi.vsync_back_porch   = 20;
  dpi.vsync_front_porch  = 10;
  /* Revision 1.x cannot run this board's 32 MiB PSRAM reliably at 200 MHz.
   * Its 80 MHz memory setting is paired with a 24 MHz pixel clock to halve
   * scanout bandwidth while preserving the complete 1024x600 active area.
   * This 30 Hz mode has been verified physically to fill the panel. */

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  dpi.dpi_clock_freq_mhz = 24;
#else
  dpi.dpi_clock_freq_mhz = 48;
#endif
  dpi.virtual_channel    = 0;
  dpi.format             = MIPI_DSI_FMT_RGB565;

  ret = esp_mipi_dsi_configure_dpi(&dpi);
  printf("DISP: dpi config -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }

  /* 6. Select the pixel source.  The host pattern is the same isolation
   * test provided by the ESP-IDF DPI panel driver. */

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_TEST_PATTERN
  ret = esp_mipi_dsi_set_test_pattern(true);
  printf("DISP: host pattern (no PSRAM/DMA) -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }
#else
  /* Framebuffer from PSRAM-backed heap */

  fb = kmm_memalign(64, FB_SIZE);
  if (fb == NULL)
    {
      printf("DISP: cannot allocate %u-byte RGB565 framebuffer\n",
             (unsigned int)FB_SIZE);
      return -ENOMEM;
    }

  fb16 = (FAR uint16_t *)fb;
  printf("DISP: RGB565 framebuffer @%p size=%u\n", fb,
         (unsigned int)FB_SIZE);

  /* Eight full-height RGB565 bars make line width, channel order, and the
   * amount of active image immediately identifiable on the physical LCD. */

  static const uint16_t bars[8] =
  {
    0x0000, 0xf800, 0x07e0, 0x001f,
    0x07ff, 0xf81f, 0xffe0, 0xffff
  };

  for (i = 0; i < FB_W * FB_H; i++)
    {
      unsigned int bar = (i % FB_W) / (FB_W / 8);
      fb16[i] = bars[bar];
    }

#ifdef CONFIG_ESPRESSIF_MIPI_DSI_FIXED_SOURCE_TEST
  /* A fixed-source DMA repeats these four RGB565 red pixels for the entire
   * transfer.  A full red panel proves bridge/Host framing independently of
   * sequential PSRAM access. */

  fb16[0] = 0xf800;
  fb16[1] = 0xf800;
  fb16[2] = 0xf800;
  fb16[3] = 0xf800;
#endif

  ret = esp_mipi_dsi_bind_framebuffer(fb, FB_SIZE, FB_W, FB_H, FB_BPP);
  printf("DISP: fb bind -> %d\n", ret);
  if (ret != OK)
    {
      kmm_free(fb);
      return ret;
    }

  ret = esp_mipi_dsi_flush_framebuffer(fb, FB_SIZE);
  printf("DISP: fb flush -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }

  lcd_progress("L2\n");
#endif

  /* 7. Go live, then backlight on */

  ret = esp_mipi_dsi_video_start();
  printf("DISP: video start -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }

  lcd_progress("L3\n");

  esp_configgpio(LCD_BL_GPIO, OUTPUT);
  esp_gpiowrite(LCD_BL_GPIO, true);
  printf("DISP: backlight on after video start\n");

  nxsig_usleep(250 * 1000);
  ret = esp_mipi_dsi_get_diagnostics(&dma_frames, &dma_status,
                                     &bridge_status, &lane_mbps_x100,
                                     &dpi_expect_mhz_x100,
                                     &dpi_real_mhz_x100, &host_int_st0,
                                     &host_int_st1, &host_vid_pkt_status,
                                     &host_phy_status, &host_color_coding);
  printf("DISP: 250ms frames=%lu dma=%08lx bridge=%08lx "
         "lane=%lu.%02luMbps dpi=%lu.%02lu/%lu.%02luMHz -> %d\n",
         (unsigned long)dma_frames, (unsigned long)dma_status,
         (unsigned long)bridge_status,
         (unsigned long)(lane_mbps_x100 / 100),
         (unsigned long)(lane_mbps_x100 % 100),
         (unsigned long)(dpi_real_mhz_x100 / 100),
         (unsigned long)(dpi_real_mhz_x100 % 100),
         (unsigned long)(dpi_expect_mhz_x100 / 100),
         (unsigned long)(dpi_expect_mhz_x100 % 100), ret);
  printf("DISP: host int0=%08lx int1=%08lx vid=%08lx phy=%08lx color=%08lx\n",
         (unsigned long)host_int_st0, (unsigned long)host_int_st1,
         (unsigned long)host_vid_pkt_status, (unsigned long)host_phy_status,
         (unsigned long)host_color_coding);
  lcd_progress("L4\n");

  ret = esp_mipi_dsi_get_timing_diagnostics(&timing_diag);
  printf("DISP: brg words=%lu fifo=%08lx pix=%08lx "
         "h=%lu/%lu/%lu/%lu v=%lu/%lu/%lu/%lu -> %d\n",
         (unsigned long)timing_diag.bridge_raw_words,
         (unsigned long)timing_diag.bridge_fifo_status,
         (unsigned long)timing_diag.bridge_pixel_type,
         (unsigned long)timing_diag.bridge_hactive,
         (unsigned long)timing_diag.bridge_htotal,
         (unsigned long)timing_diag.bridge_hsync,
         (unsigned long)timing_diag.bridge_hback_porch,
         (unsigned long)timing_diag.bridge_vactive,
         (unsigned long)timing_diag.bridge_vtotal,
         (unsigned long)timing_diag.bridge_vsync,
         (unsigned long)timing_diag.bridge_vback_porch, ret);
  printf("DISP: host cfg pkt=%lu h=%lu/%lu/%lu "
         "v=%lu/%lu/%lu/%lu\n",
         (unsigned long)timing_diag.host_packet_pixels,
         (unsigned long)timing_diag.host_hsync_time,
         (unsigned long)timing_diag.host_hback_porch_time,
         (unsigned long)timing_diag.host_hline_time,
         (unsigned long)timing_diag.host_vsync_lines,
         (unsigned long)timing_diag.host_vback_porch_lines,
         (unsigned long)timing_diag.host_vfront_porch_lines,
         (unsigned long)timing_diag.host_vactive_lines);

  nxsig_usleep(5 * 1000 * 1000);
  ret = esp_mipi_dsi_get_diagnostics(&dma_frames, &dma_status,
                                     &bridge_status, &lane_mbps_x100,
                                     &dpi_expect_mhz_x100,
                                     &dpi_real_mhz_x100, &host_int_st0,
                                     &host_int_st1, &host_vid_pkt_status,
                                     &host_phy_status, &host_color_coding);
  printf("DISP: 5s frames=%lu dma=%08lx bridge=%08lx "
         "host=%08lx/%08lx vid=%08lx phy=%08lx -> %d\n",
         (unsigned long)dma_frames, (unsigned long)dma_status,
         (unsigned long)bridge_status, (unsigned long)host_int_st0,
         (unsigned long)host_int_st1,
         (unsigned long)host_vid_pkt_status,
         (unsigned long)host_phy_status, ret);

  memset(&dma_diag, 0, sizeof(dma_diag));
  ret = esp_mipi_dsi_get_dma_diagnostics(&dma_diag);
  printf("DISP: dma amount=%lu live=%lu fifo=%lu/%lu common=%08lx "
         "lli=%08lx src=%08lx block=%lu ctl=%08lx/%08lx -> %d\n",
         (unsigned long)dma_diag.last_transfer_items,
         (unsigned long)dma_diag.live_transfer_items,
         (unsigned long)dma_diag.last_fifo_items,
         (unsigned long)dma_diag.live_fifo_items,
         (unsigned long)dma_diag.last_common_status,
         (unsigned long)dma_diag.current_lli,
         (unsigned long)dma_diag.lli_source,
         (unsigned long)dma_diag.lli_block_items,
         (unsigned long)dma_diag.lli_control_low,
         (unsigned long)dma_diag.lli_control_high, ret);
  lcd_progress("L5\n");

  g_fb     = fb;
  g_ready  = true;

#ifndef CONFIG_ESPRESSIF_MIPI_DSI_TEST_PATTERN
  ret = fb_register(0, 0);
  printf("DISP: /dev/fb0 register -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }
#endif

  esp_configgpio(LCD_BL_GPIO, OUTPUT);
  esp_gpiowrite(LCD_BL_GPIO, true);
  printf("DISP: backlight already on\n");
  lcd_progress("L6\n");

  return OK;
}


/****************************************************************************
 * Framebuffer character device glue (/dev/fb0) for LVGL & apps.
 * The pixel surface is the PSRAM buffer already bound to DW-GDMA.
 ****************************************************************************/

static int disp_getvideoinfo(FAR struct fb_vtable_s *vtable,
                             FAR struct fb_videoinfo_s *vinfo)
{
  vinfo->fmt     = FB_FMT_RGB16_565;
  vinfo->xres    = FB_W;
  vinfo->yres    = FB_H;
  vinfo->nplanes = 1;
  return OK;
}

static int disp_getplaneinfo(FAR struct fb_vtable_s *vtable, int planeno,
                             FAR struct fb_planeinfo_s *pinfo)
{
  if (planeno != 0)
    {
      return -EINVAL;
    }

  pinfo->fbmem   = (FAR void *)g_fb;
  pinfo->fblen   = FB_SIZE;
  pinfo->stride   = FB_W * FB_BYTES_PER_PIXEL;
  pinfo->display  = 0;
  pinfo->bpp      = FB_BPP;
  return OK;
}

#ifdef CONFIG_FB_UPDATE
static int disp_updatearea(FAR struct fb_vtable_s *vtable,
                           FAR const struct fb_area_s *area)
{
  FAR uint8_t *start;
  size_t len;

  if (g_fb == NULL || area == NULL || area->h == 0 ||
      area->y >= FB_H || area->h > FB_H - area->y)
    {
      return -EINVAL;
    }

  /* The DMA always scans a complete frame, but only rows touched by LVGL
   * need CPU-to-memory cache writeback.  Flush complete scanlines so the
   * range remains simple and cache-line safe even for a narrow dirty area.
   * This follows ESP-IDF's DPI framebuffer update strategy and avoids a
   * 1.2 MiB writeback for every small clock or touch-label update. */

  start = g_fb + (size_t)area->y * FB_W * FB_BYTES_PER_PIXEL;
  len = (size_t)area->h * FB_W * FB_BYTES_PER_PIXEL;

  g_update_count++;
  if (g_update_count <= 4)
    {
      printf("DISP: update #%lu x=%u y=%u w=%u h=%u flush=%p+%u\n",
             (unsigned long)g_update_count, area->x, area->y,
             area->w, area->h, start, (unsigned int)len);
    }

  return esp_mipi_dsi_flush_framebuffer(start, len);
}
#endif

static struct fb_vtable_s g_disp_vtable =
{
  .getvideoinfo = disp_getvideoinfo,
  .getplaneinfo = disp_getplaneinfo,
#ifdef CONFIG_FB_UPDATE
  .updatearea   = disp_updatearea,
#endif
};

/* Arch hooks used by fb_register() when CONFIG_VIDEO_FB is enabled */

int up_fbinitialize(int display)
{
  return esp32p4_display_init();
}

FAR struct fb_vtable_s *up_fbgetvplane(int display, int plane)
{
  return g_ready ? &g_disp_vtable : NULL;
}
