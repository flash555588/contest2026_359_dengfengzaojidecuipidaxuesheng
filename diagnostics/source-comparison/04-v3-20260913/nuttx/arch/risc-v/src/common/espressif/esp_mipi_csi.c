/****************************************************************************
 * arch/risc-v/src/common/espressif/esp_mipi_csi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <sys/time.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/video/imgdata.h>
#include <nuttx/wqueue.h>

#include "driver/isp_ccm.h"
#include "driver/isp_color.h"
#include "driver/isp_core.h"
#include "driver/isp_demosaic.h"
#include "esp_attr.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_heap_caps.h"
#include "esp_mipi_csi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CSI_WIDTH              1024
#define CSI_HEIGHT             600
#define CSI_FRAME_RATE         30
#define CSI_DATA_LANES         2
#define CSI_LANE_RATE_MBPS     288
#define CSI_ISP_CLOCK_HZ       80000000
#define CSI_DMA_ALIGNMENT      64
#define CSI_FRAME_SIZE         (CSI_WIDTH * CSI_HEIGHT * sizeof(uint16_t))

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Zero-copy capture: CSI DMA writes straight into the V4L2 USERPTR page.
 * V4L2 hands the driver exactly one page at a time and supplies the next
 * one from inside the completion callback, so the callback is invoked
 * synchronously from the CSI "need new buffer" ISR.  When no page is
 * pending the HAL falls back to its own backup buffer (frame dropped).
 */

struct esp_mipi_csi_s
{
  struct imgdata_s data;
  esp_cam_ctlr_handle_t controller;
  isp_proc_handle_t isp;
  FAR uint8_t *pending_buffer;     /* Page from set_buf, not yet in DMA */
  FAR uint8_t *active_buffer;      /* Page currently written by CSI DMA */
  imgdata_capture_t callback;
  FAR void *callback_arg;
  struct work_s stop_work;
  spinlock_t lock;
  uint32_t frames_completed;
  uint32_t frames_delivered;
  uint32_t frames_dropped;
  uint32_t stop_deferred;
  bool initialized;
  bool controller_enabled;
  bool isp_enabled;
  bool capturing;
  bool stop_pending;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int csi_init(FAR struct imgdata_s *data);
static int csi_uninit(FAR struct imgdata_s *data);
static int csi_set_buf(FAR struct imgdata_s *data, uint8_t nr_datafmts,
                       FAR imgdata_format_t *datafmts, FAR uint8_t *addr,
                       uint32_t size);
static int csi_validate(FAR struct imgdata_s *data, uint8_t nr_datafmts,
                        FAR imgdata_format_t *datafmts,
                        FAR imgdata_interval_t *interval);
static int csi_start(FAR struct imgdata_s *data, uint8_t nr_datafmts,
                     FAR imgdata_format_t *datafmts,
                     FAR imgdata_interval_t *interval,
                     imgdata_capture_t callback, FAR void *arg);
static int csi_stop(FAR struct imgdata_s *data);
static FAR void *csi_alloc(FAR struct imgdata_s *data, uint32_t align_size,
                           uint32_t size);
static void csi_free(FAR struct imgdata_s *data, FAR void *addr);
static void csi_stop_worker(FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct imgdata_ops_s g_csi_ops =
{
  .init = csi_init,
  .uninit = csi_uninit,
  .set_buf = csi_set_buf,
  .validate_frame_setting = csi_validate,
  .start_capture = csi_start,
  .stop_capture = csi_stop,
  .alloc = csi_alloc,
  .free = csi_free,
};

static struct esp_mipi_csi_s g_csi =
{
  .data =
  {
    .ops = &g_csi_ops,
  },
  .lock = SP_UNLOCKED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int csi_error(esp_err_t err)
{
  if (err == ESP_ERR_NO_MEM)
    {
      return -ENOMEM;
    }

  if (err == ESP_ERR_INVALID_ARG)
    {
      return -EINVAL;
    }

  if (err == ESP_ERR_INVALID_STATE)
    {
      return -EBUSY;
    }

  if (err == ESP_ERR_TIMEOUT)
    {
      return -ETIMEDOUT;
    }

  return -EIO;
}

/* ESP32-P4 v1.0 has no ISP white-balance gain block, so red/blue gains go
 * through the CCM diagonal instead; without them RAW output looks green.
 */

static esp_err_t csi_isp_pipeline_enable(FAR struct esp_mipi_csi_s *priv)
{
  esp_isp_demosaic_config_t demosaic;
  esp_isp_ccm_config_t ccm;
  esp_isp_color_config_t color;
  esp_err_t err;

  memset(&demosaic, 0, sizeof(demosaic));
  demosaic.grad_ratio.integer = 1;
  err = esp_isp_demosaic_configure(priv->isp, &demosaic);
  if (err == ESP_OK)
    {
      err = esp_isp_demosaic_enable(priv->isp);
    }

  if (err != ESP_OK)
    {
      syslog(LOG_ERR, "CSI: ISP demosaic failed: %d\n", err);
      return err;
    }

  memset(&ccm, 0, sizeof(ccm));
  ccm.saturation = true;
  ccm.matrix[0][0] = 1.70f;
  ccm.matrix[1][1] = 0.95f;
  ccm.matrix[2][2] = 1.55f;
  err = esp_isp_ccm_configure(priv->isp, &ccm);
  if (err == ESP_OK)
    {
      err = esp_isp_ccm_enable(priv->isp);
    }

  if (err != ESP_OK)
    {
      syslog(LOG_ERR, "CSI: ISP ccm failed: %d\n", err);
      return err;
    }

  memset(&color, 0, sizeof(color));
  color.color_contrast.val = 128;
  color.color_saturation.val = 128;
  color.color_hue = 0;
  color.color_brightness = 0;
  err = esp_isp_color_configure(priv->isp, &color);
  if (err == ESP_OK)
    {
      err = esp_isp_color_enable(priv->isp);
    }

  if (err != ESP_OK)
    {
      syslog(LOG_ERR, "CSI: ISP color failed: %d\n", err);
    }

  return err;
}

static void csi_isp_pipeline_disable(FAR struct esp_mipi_csi_s *priv)
{
  if (priv->isp == NULL)
    {
      return;
    }

  esp_isp_color_disable(priv->isp);
  esp_isp_ccm_disable(priv->isp);
  esp_isp_demosaic_disable(priv->isp);
}

static void csi_hw_stop(FAR struct esp_mipi_csi_s *priv)
{
  irqstate_t flags;
  bool do_stop;

  flags = spin_lock_irqsave(&priv->lock);
  do_stop = priv->stop_pending;
  priv->stop_pending = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (do_stop)
    {
      esp_cam_ctlr_stop(priv->controller);
    }
}

static void csi_stop_worker(FAR void *arg)
{
  csi_hw_stop((FAR struct esp_mipi_csi_s *)arg);
}

static bool IRAM_ATTR csi_get_new_buffer(esp_cam_ctlr_handle_t handle,
                                         FAR esp_cam_ctlr_trans_t *trans,
                                         FAR void *arg)
{
  FAR struct esp_mipi_csi_s *priv = arg;
  FAR uint8_t *done;
  FAR void *callback_arg;
  imgdata_capture_t callback;
  struct timeval timestamp;
  irqstate_t flags;

  UNUSED(handle);

  /* The previous transaction's DMA has completed when the HAL asks for a
   * new buffer, so deliver it now.  V4L2 calls set_buf() for the next page
   * from inside the callback, which is what lets the next frame start
   * without a gap.
   */

  flags = spin_lock_irqsave(&priv->lock);
  done = priv->active_buffer;
  priv->active_buffer = NULL;
  callback = priv->capturing ? priv->callback : NULL;
  callback_arg = priv->callback_arg;
  if (done != NULL)
    {
      priv->frames_completed++;
      if (callback == NULL)
        {
          priv->frames_dropped++;
        }
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (done != NULL && callback != NULL)
    {
      gettimeofday(&timestamp, NULL);
      callback(0, CSI_FRAME_SIZE, &timestamp, callback_arg);
      flags = spin_lock_irqsave(&priv->lock);
      priv->frames_delivered++;
      spin_unlock_irqrestore(&priv->lock, flags);
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->capturing && priv->pending_buffer != NULL)
    {
      priv->active_buffer = priv->pending_buffer;
      priv->pending_buffer = NULL;
      trans->buffer = priv->active_buffer;
      trans->buflen = CSI_FRAME_SIZE;
    }
  else if (priv->capturing)
    {
      priv->frames_dropped++;
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return false;
}

static bool IRAM_ATTR csi_buffer_finished(esp_cam_ctlr_handle_t handle,
                                          FAR esp_cam_ctlr_trans_t *trans,
                                          FAR void *arg)
{
  /* Frames are delivered from csi_get_new_buffer; nothing left to do. */

  UNUSED(handle);
  UNUSED(trans);
  UNUSED(arg);
  return false;
}

static int csi_init(FAR struct imgdata_s *data)
{
  FAR struct esp_mipi_csi_s *priv =
    (FAR struct esp_mipi_csi_s *)data;
  esp_cam_ctlr_csi_config_t csi_config;
  esp_cam_ctlr_evt_cbs_t callbacks;
  esp_isp_processor_cfg_t isp_config;
  esp_err_t err;

  if (priv->initialized)
    {
      return OK;
    }

  memset(&csi_config, 0, sizeof(csi_config));
  csi_config.ctlr_id = 0;
  csi_config.h_res = CSI_WIDTH;
  csi_config.v_res = CSI_HEIGHT;
  csi_config.lane_bit_rate_mbps = CSI_LANE_RATE_MBPS;
  csi_config.input_data_color_type = CAM_CTLR_COLOR_RAW8;
  csi_config.output_data_color_type = CAM_CTLR_COLOR_RAW8;
  csi_config.data_lane_num = CSI_DATA_LANES;
  csi_config.queue_items = 1;
  err = esp_cam_new_csi_ctlr(&csi_config, &priv->controller);
  if (err != ESP_OK)
    {
      syslog(LOG_ERR, "CSI: controller create failed: %d\n", err);
      return csi_error(err);
    }

  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.on_get_new_trans = csi_get_new_buffer;
  callbacks.on_trans_finished = csi_buffer_finished;
  err = esp_cam_ctlr_register_event_callbacks(priv->controller, &callbacks,
                                               priv);
  if (err != ESP_OK)
    {
      syslog(LOG_ERR, "CSI: callback registration failed: %d\n", err);
      goto fail;
    }

  err = esp_cam_ctlr_enable(priv->controller);
  if (err != ESP_OK)
    {
      syslog(LOG_ERR, "CSI: controller enable failed: %d\n", err);
      goto fail;
    }

  priv->controller_enabled = true;

  memset(&isp_config, 0, sizeof(isp_config));
  isp_config.clk_hz = CSI_ISP_CLOCK_HZ;
  isp_config.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
  isp_config.input_data_color_type = ISP_COLOR_RAW8;
  isp_config.output_data_color_type = ISP_COLOR_RGB565;
  isp_config.h_res = CSI_WIDTH;
  isp_config.v_res = CSI_HEIGHT;
  isp_config.bayer_order = COLOR_RAW_ELEMENT_ORDER_BGGR;
  err = esp_isp_new_processor(&isp_config, &priv->isp);
  if (err != ESP_OK)
    {
      syslog(LOG_ERR, "CSI: ISP create failed: %d\n", err);
      goto fail;
    }

  err = csi_isp_pipeline_enable(priv);
  if (err != ESP_OK)
    {
      goto fail;
    }

  err = esp_isp_enable(priv->isp);
  if (err != ESP_OK)
    {
      syslog(LOG_ERR, "CSI: ISP enable failed: %d\n", err);
      goto fail;
    }

  priv->isp_enabled = true;
  priv->initialized = true;
  syslog(LOG_INFO,
         "CSI: HAL ready, 1024x600 RAW8 to RGB565, direct USERPTR capture\n");
  return OK;

fail:
  if (priv->isp != NULL)
    {
      if (priv->isp_enabled)
        {
          esp_isp_disable(priv->isp);
          priv->isp_enabled = false;
        }

      csi_isp_pipeline_disable(priv);
      esp_isp_del_processor(priv->isp);
      priv->isp = NULL;
    }

  if (priv->controller != NULL)
    {
      if (priv->controller_enabled)
        {
          esp_cam_ctlr_disable(priv->controller);
          priv->controller_enabled = false;
        }

      esp_cam_ctlr_del(priv->controller);
      priv->controller = NULL;
    }

  return csi_error(err);
}

static int csi_uninit(FAR struct imgdata_s *data)
{
  FAR struct esp_mipi_csi_s *priv =
    (FAR struct esp_mipi_csi_s *)data;
  esp_err_t err;
  int ret = OK;

  if (!priv->initialized)
    {
      return OK;
    }

  csi_stop(data);
  work_cancel_sync(LPWORK, &priv->stop_work);
  csi_hw_stop(priv);

  if (priv->isp_enabled)
    {
      err = esp_isp_disable(priv->isp);
      if (err != ESP_OK && ret == OK)
        {
          ret = csi_error(err);
        }

      priv->isp_enabled = false;
    }

  if (priv->isp != NULL)
    {
      csi_isp_pipeline_disable(priv);
      err = esp_isp_del_processor(priv->isp);
      if (err != ESP_OK && ret == OK)
        {
          ret = csi_error(err);
        }

      priv->isp = NULL;
    }

  if (priv->controller_enabled)
    {
      err = esp_cam_ctlr_disable(priv->controller);
      if (err != ESP_OK && ret == OK)
        {
          ret = csi_error(err);
        }

      priv->controller_enabled = false;
    }

  if (priv->controller != NULL)
    {
      err = esp_cam_ctlr_del(priv->controller);
      if (err != ESP_OK && ret == OK)
        {
          ret = csi_error(err);
        }

      priv->controller = NULL;
    }

  priv->initialized = false;
  return ret;
}

static int csi_set_buf(FAR struct imgdata_s *data, uint8_t nr_datafmts,
                       FAR imgdata_format_t *datafmts, FAR uint8_t *addr,
                       uint32_t size)
{
  FAR struct esp_mipi_csi_s *priv =
    (FAR struct esp_mipi_csi_s *)data;
  irqstate_t flags;
  int ret;

  ret = csi_validate(data, nr_datafmts, datafmts, NULL);
  if (ret < 0 || addr == NULL || size < CSI_FRAME_SIZE ||
      ((uintptr_t)addr & (CSI_DMA_ALIGNMENT - 1)) != 0)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  priv->pending_buffer = addr;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int csi_validate(FAR struct imgdata_s *data, uint8_t nr_datafmts,
                        FAR imgdata_format_t *datafmts,
                        FAR imgdata_interval_t *interval)
{
  UNUSED(data);

  if (nr_datafmts != 1 || datafmts == NULL ||
      datafmts[0].width != CSI_WIDTH ||
      datafmts[0].height != CSI_HEIGHT ||
      datafmts[0].pixelformat != IMGDATA_PIX_FMT_RGB565)
    {
      return -EINVAL;
    }

  if (interval != NULL &&
      (interval->numerator != 1 ||
       interval->denominator != CSI_FRAME_RATE))
    {
      return -EINVAL;
    }

  return OK;
}

static int csi_start(FAR struct imgdata_s *data, uint8_t nr_datafmts,
                     FAR imgdata_format_t *datafmts,
                     FAR imgdata_interval_t *interval,
                     imgdata_capture_t callback, FAR void *arg)
{
  FAR struct esp_mipi_csi_s *priv =
    (FAR struct esp_mipi_csi_s *)data;
  irqstate_t flags;
  esp_err_t err;

  if (!priv->initialized || callback == NULL ||
      csi_validate(data, nr_datafmts, datafmts, interval) < 0)
    {
      return -EINVAL;
    }

  /* A stop requested from ISR context may still be pending; finish it
   * here in task context before restarting the controller.
   */

  csi_hw_stop(priv);

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->capturing || priv->pending_buffer == NULL)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }

  priv->active_buffer = NULL;
  priv->callback = callback;
  priv->callback_arg = arg;
  priv->capturing = true;
  spin_unlock_irqrestore(&priv->lock, flags);

  /* esp_cam_ctlr_start() pulls the first page through csi_get_new_buffer. */

  err = esp_cam_ctlr_start(priv->controller);
  if (err != ESP_OK)
    {
      flags = spin_lock_irqsave(&priv->lock);
      priv->capturing = false;
      priv->callback = NULL;
      priv->callback_arg = NULL;
      priv->active_buffer = NULL;
      spin_unlock_irqrestore(&priv->lock, flags);
      return csi_error(err);
    }

  return OK;
}

static int csi_stop(FAR struct imgdata_s *data)
{
  FAR struct esp_mipi_csi_s *priv =
    (FAR struct esp_mipi_csi_s *)data;
  irqstate_t flags;
  bool was_capturing;

  flags = spin_lock_irqsave(&priv->lock);
  was_capturing = priv->capturing;
  priv->capturing = false;
  priv->callback = NULL;
  priv->callback_arg = NULL;
  priv->pending_buffer = NULL;
  priv->active_buffer = NULL;
  if (was_capturing)
    {
      priv->stop_pending = true;
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (!was_capturing)
    {
      return OK;
    }

  /* V4L2 calls stop_capture from the completion callback, which here runs
   * inside the CSI HAL ISR; the HAL re-arms DMA after our callback, so the
   * controller must be stopped later from task context.
   */

  if (up_interrupt_context())
    {
      flags = spin_lock_irqsave(&priv->lock);
      priv->stop_deferred++;
      spin_unlock_irqrestore(&priv->lock, flags);
      work_queue(LPWORK, &priv->stop_work, csi_stop_worker, priv, 0);
      return OK;
    }

  csi_hw_stop(priv);
  return OK;
}

static FAR void *csi_alloc(FAR struct imgdata_s *data, uint32_t align_size,
                           uint32_t size)
{
  size_t alignment = align_size > CSI_DMA_ALIGNMENT ?
                     align_size : CSI_DMA_ALIGNMENT;

  UNUSED(data);
  return heap_caps_aligned_calloc(alignment, 1, size,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void csi_free(FAR struct imgdata_s *data, FAR void *addr)
{
  UNUSED(data);
  heap_caps_free(addr);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct imgdata_s *esp_mipi_csi_initialize(void)
{
  return &g_csi.data;
}

void esp_mipi_csi_dump_status(void)
{
  irqstate_t flags;
  uint32_t completed;
  uint32_t delivered;
  uint32_t dropped;
  uint32_t deferred;

  flags = spin_lock_irqsave(&g_csi.lock);
  completed = g_csi.frames_completed;
  delivered = g_csi.frames_delivered;
  dropped = g_csi.frames_dropped;
  deferred = g_csi.stop_deferred;
  spin_unlock_irqrestore(&g_csi.lock, flags);

  syslog(LOG_INFO,
         "CSI: completed=%lu delivered=%lu dropped=%lu stop_deferred=%lu\n",
         (unsigned long)completed, (unsigned long)delivered,
         (unsigned long)dropped, (unsigned long)deferred);
}
