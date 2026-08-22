/****************************************************************************
 * boards/espressif/esp32p4/esp32p4-function-ev-board/src/esp32p4_display.c
 *
 * Simple Boot display bring-up for the 7" 1024x600 MIPI-DSI panel
 * (EK79007-class driver IC on the LCD adapter board).
 *
 * Sequence: power DPHY LDO -> DSI host init (2 lanes @900Mbps) -> panel
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
#define FB_SIZE            (FB_W * FB_H * FB_BPP / 8)

/* Diagnostic A/B: the DSI Host pattern generator bypasses PSRAM and
 * DW-GDMA.  Keep this enabled until the 1/4-screen failure is classified. */

#define LCD_HOST_PATTERN_TEST 1

static FAR uint16_t *g_fb;
static volatile bool g_ready;

/* EK79007 vendor registers */
#define EK79007_PAD_CONTROL 0xB2
#define EK79007_DSI_2_LANE  0x10
#define EK79007_CMD_SLPOUT  0x11
#define EK79007_CMD_DISPON  0x29

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
  uint8_t lane_readback;
  uint8_t power_mode;
  ssize_t read_ret;
  FAR uint16_t *fb = NULL;
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
  int ret;

  if (g_ready)
    {
      return OK;
    }

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

  /* 2. DSI host: use the EK79007 vendor driver's 900 Mbps default.  The
   * early ESP32-P4 rev v1.0 has less HS margin than production silicon. */

  memset(&bus, 0, sizeof(bus));
  bus.num_data_lanes     = 2;
  bus.lane_bit_rate_mbps = 900;

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

  /* 3. Panel hardware reset, active low */

  esp_configgpio(LCD_RST_GPIO, OUTPUT);
  esp_gpiowrite(LCD_RST_GPIO, false);
  nxsig_usleep(10 * 1000);
  esp_gpiowrite(LCD_RST_GPIO, true);
  nxsig_usleep(20 * 1000);

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
  dpi.hsync_back_porch   = 160;
  dpi.hsync_front_porch  = 160;
  dpi.vsync_pulse_width  = 1;
  dpi.vsync_back_porch   = 23;
  dpi.vsync_front_porch  = 12;
  dpi.dpi_clock_freq_mhz = 52;
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

#if LCD_HOST_PATTERN_TEST
  ret = esp_mipi_dsi_set_test_pattern(true);
  printf("DISP: host pattern (no PSRAM/DMA) -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }
#else
  /* Framebuffer from PSRAM-backed heap */

  fb = kmm_malloc(FB_SIZE);
  if (fb == NULL)
    {
      /* Heap fallback: carve straight from the mapped PSRAM window.
       * The kernel heap currently does not hand out PSRAM, so the
       * beginning of the window is guaranteed untouched. */
      extern uintptr_t esp_psram_extram_vaddr_start(void);
      uintptr_t p = esp_psram_extram_vaddr_start();
      printf("DISP: heap short, carve fb @%08lx\n", (unsigned long)p);
      fb = (FAR uint16_t *)p;
    }

  /* Eight full-height RGB565 bars make line width, color order, and the
   * amount of active image immediately identifiable on the physical LCD. */

  static const uint16_t bars[8] =
  {
    0x0000, 0xf800, 0x07e0, 0x001f,
    0x07ff, 0xf81f, 0xffe0, 0xffff
  };

  for (i = 0; i < FB_W * FB_H; i++)
    {
      uint16_t x = i % FB_W;
      fb[i] = bars[x / (FB_W / 8)];
    }

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
#endif

  /* 7. Go live, then backlight on */

  ret = esp_mipi_dsi_video_start();
  printf("DISP: video start -> %d\n", ret);
  if (ret != OK)
    {
      return ret;
    }

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

  g_fb     = fb;
  g_ready  = true;

#if !LCD_HOST_PATTERN_TEST
  ret = fb_register(0, 0);
  printf("DISP: /dev/fb0 register -> %d\n", ret);
#endif

  esp_configgpio(LCD_BL_GPIO, OUTPUT);
  esp_gpiowrite(LCD_BL_GPIO, true);
  printf("DISP: backlight on\n");

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
  pinfo->stride   = FB_W * 2;
  pinfo->display  = 0;
  pinfo->bpp      = FB_BPP;
  return OK;
}

#ifdef CONFIG_FB_UPDATE
static int disp_updatearea(FAR struct fb_vtable_s *vtable,
                           FAR const struct fb_area_s *area)
{
  esp_mipi_dsi_flush_framebuffer(g_fb, FB_SIZE);
  return OK;
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
