/* SPDX-License-Identifier: Apache-2.0
 * Cache-on control of autonomous scanout. Never runs an IRQ with cache off.
 * P4 TRM 7.5.4.2: reload without completion events; end by clearing both
 * multi-block types, then wait for the final transfer-done boundary.
 */

#include <nuttx/signal.h>

#define DSI_RELOAD_IDLE       0
#define DSI_RELOAD_REQUESTED  1
#define DSI_RELOAD_ACTIVE     2
#define DSI_RELOAD_ENDING     3
#define DSI_RELOAD_NO_STALL   (DW_GDMA_LL_CHANNEL_EVENT_BLOCK_TFR_DONE | \
                             DW_GDMA_LL_CHANNEL_EVENT_SRC_TRANSCOMP | \
                             DW_GDMA_LL_CHANNEL_EVENT_DST_TRANSCOMP)

static mutex_t g_dsi_flash_lock = NXMUTEX_INITIALIZER;
static bool g_dsi_reload_qualified;
static uint32_t g_dsi_reload_count;

static void esp_mipi_dsi_reload_wait_until(uint64_t deadline)
{
  uint64_t now;
  while ((now = esp_camera_timing_now()) < deadline)
    {
      uint64_t remaining = deadline - now;
      nxsig_usleep(remaining > 100000 ? 100000 : remaining);
    }
}

/* Called only at TFR_DONE with the old channel disabled and dmalock held. */

static void esp_mipi_dsi_reload_start(struct esp_mipi_dsi_priv_s *priv)
{
  dw_gdma_dev_t *dev = priv->dw_hal.dev;
  dw_gdma_link_list_item_t *lli = priv->lli_nc;

  priv->reload_status_mask = dev->ch[ESP_MIPI_DSI_DMA_CHAN].int_st_ena0.val;
  dw_gdma_ll_channel_enable_intr_generation(dev, ESP_MIPI_DSI_DMA_CHAN,
                                           DSI_RELOAD_NO_STALL, false);
  dw_gdma_ll_channel_clear_intr(dev, ESP_MIPI_DSI_DMA_CHAN, UINT32_MAX);
  dw_gdma_ll_channel_set_src_addr(dev, ESP_MIPI_DSI_DMA_CHAN,
                                 (uint32_t)(uintptr_t)priv->fb);
  dw_gdma_ll_channel_set_dst_addr(dev, ESP_MIPI_DSI_DMA_CHAN,
                                 MIPI_DSI_BRG_MEM_BASE);
  dev->ch[ESP_MIPI_DSI_DMA_CHAN].block_ts0.val = lli->block_ts_lo.val;
  dev->ch[ESP_MIPI_DSI_DMA_CHAN].ctl0.val = lli->ctrl_lo.val;
  dev->ch[ESP_MIPI_DSI_DMA_CHAN].ctl1.val = lli->ctrl_hi.val;
  dw_gdma_ll_channel_set_block_markers(dev, ESP_MIPI_DSI_DMA_CHAN,
                                     false, false, false);
  dev->ch[ESP_MIPI_DSI_DMA_CHAN].cfg0.src_multblk_type =
    DW_GDMA_BLOCK_TRANSFER_RELOAD;
  dev->ch[ESP_MIPI_DSI_DMA_CHAN].cfg0.dst_multblk_type =
    DW_GDMA_BLOCK_TRANSFER_RELOAD;
  priv->reload_started = esp_camera_timing_now();
  priv->reload_phase = DSI_RELOAD_ACTIVE;
  dw_gdma_ll_channel_enable(dev, ESP_MIPI_DSI_DMA_CHAN, true);
}

/* The same task mutex also encloses pan and video stop. It never nests
 * inside an SDK cache guard, and the ISR never takes this mutex.
 */

static int esp_mipi_dsi_reload_begin(bool probe)
{
  struct esp_mipi_dsi_priv_s *priv = &g_esp_mipi_dsi;
  int ret = nxmutex_lock(&g_dsi_flash_lock);
  if (ret < 0) return ret;
  irqstate_t flags = spin_lock_irqsave(&priv->dmalock);
  if (priv->reload_phase != DSI_RELOAD_IDLE)
    {
      spin_unlock_irqrestore(&priv->dmalock, flags);
      nxmutex_unlock(&g_dsi_flash_lock);
      return -EBUSY;
    }
  if ((!probe && !g_dsi_reload_qualified) || !priv->dma_enabled)
    {
      spin_unlock_irqrestore(&priv->dmalock, flags);
      nxmutex_unlock(&g_dsi_flash_lock);
      return 0;
    }

  if (!priv->reload_frame_us)
    {
      spin_unlock_irqrestore(&priv->dmalock, flags);
      nxmutex_unlock(&g_dsi_flash_lock);
      return -EBUSY;
    }

  nxsem_reset(&priv->reload_done, 0);
  priv->reload_error = false;
  if (probe) g_dsi_reload_qualified = false;
  priv->reload_phase = DSI_RELOAD_REQUESTED;
  spin_unlock_irqrestore(&priv->dmalock, flags);
  ret = nxsem_tickwait_uninterruptible(&priv->reload_done, MSEC2TICK(200));
  flags = spin_lock_irqsave(&priv->dmalock);
  if (priv->reload_phase == DSI_RELOAD_ACTIVE)
    {
      /* The IRQ may have accepted exactly at the timeout boundary. */
      ret = 1;
    }
  else
    {
      if (priv->reload_phase == DSI_RELOAD_REQUESTED)
        priv->reload_phase = DSI_RELOAD_IDLE;
      ret = ret < 0 ? ret : -EIO;
    }

  spin_unlock_irqrestore(&priv->dmalock, flags);
  if (ret < 0) nxmutex_unlock(&g_dsi_flash_lock);
  return ret;
}

int esp_mipi_dsi_flash_begin(void)
{
  return esp_mipi_dsi_reload_begin(false);
}

static int esp_mipi_dsi_reload_end(bool probe, bool progress)
{
  struct esp_mipi_dsi_priv_s *priv = &g_esp_mipi_dsi;
  uint64_t minimum = (uint64_t)priv->reload_frame_us * 3 + 20000;
  esp_mipi_dsi_reload_wait_until(priv->reload_started + minimum);

  irqstate_t flags = spin_lock_irqsave(&priv->dmalock);
  nxsem_reset(&priv->reload_done, 0);
  int ret = -EIO;
  if (priv->reload_phase == DSI_RELOAD_ACTIVE && priv->dma_enabled)
    {
      /* One MMIO write clears both types. Do not stop a partial frame. */
      dmac_chn_cfg0_reg_t cfg = priv->dw_hal.dev->ch[ESP_MIPI_DSI_DMA_CHAN].cfg0;
      cfg.src_multblk_type = DW_GDMA_BLOCK_TRANSFER_CONTIGUOUS;
      cfg.dst_multblk_type = DW_GDMA_BLOCK_TRANSFER_CONTIGUOUS;
      priv->reload_phase = DSI_RELOAD_ENDING;
      priv->dw_hal.dev->ch[ESP_MIPI_DSI_DMA_CHAN].cfg0.val = cfg.val;
      dw_gdma_ll_channel_clear_intr(priv->dw_hal.dev,
        ESP_MIPI_DSI_DMA_CHAN, DSI_RELOAD_NO_STALL);
      spin_unlock_irqrestore(&priv->dmalock, flags);
      ret = nxsem_tickwait_uninterruptible(&priv->reload_done, MSEC2TICK(200));
      flags = spin_lock_irqsave(&priv->dmalock);
      if (priv->reload_phase == DSI_RELOAD_IDLE) ret = 0;
    }

  if (priv->reload_error) ret = -EIO;
  if (probe) g_dsi_reload_qualified = ret == 0 && progress;
  spin_unlock_irqrestore(&priv->dmalock, flags);
  g_dsi_reload_count++;
  printf("[scanout-hold] end=%d elapsed_us=%llu count=%lu qualified=%u\n",
         ret, (unsigned long long)(esp_camera_timing_now() - priv->reload_started),
         (unsigned long)g_dsi_reload_count, g_dsi_reload_qualified);
  nxmutex_unlock(&g_dsi_flash_lock);
  return ret;
}

int esp_mipi_dsi_flash_end(void)
{
  return esp_mipi_dsi_reload_end(false, false);
}

/* Qualification performs no Flash I/O. Require progress in two windows well
 * after a complete frame would have ended, then a confirmed LIST recovery.
 */

int esp_mipi_dsi_flash_probe(void)
{
  struct esp_mipi_dsi_priv_s *priv = &g_esp_mipi_dsi;
  int ret = esp_mipi_dsi_reload_begin(true);
  if (ret <= 0) return ret < 0 ? ret : -EAGAIN;
  uint32_t initial_irqs = g_esp_mipi_dsi_frames;
  bool progress = true;
  for (unsigned window = 0; window < 2; window++)
    {
      esp_mipi_dsi_reload_wait_until(esp_camera_timing_now() +
                                    priv->reload_frame_us * 3 + 10000);
      uint32_t first = priv->dw_hal.dev->ch[ESP_MIPI_DSI_DMA_CHAN].sar0.val;
      esp_mipi_dsi_reload_wait_until(esp_camera_timing_now() + 1000);
      uint32_t second = priv->dw_hal.dev->ch[ESP_MIPI_DSI_DMA_CHAN].sar0.val;
      uint32_t enabled = priv->dw_hal.dev->chen0.val & (1u << ESP_MIPI_DSI_DMA_CHAN);
      printf("[scanout-hold] probe window=%u sar=%08lx/%08lx enabled=%lu status=%08lx frame_irqs=%lu\n",
             window, (unsigned long)first, (unsigned long)second,
             (unsigned long)enabled,
             (unsigned long)priv->dw_hal.dev->ch[ESP_MIPI_DSI_DMA_CHAN].int_st0.val,
             (unsigned long)(g_esp_mipi_dsi_frames - initial_irqs));
      if (!enabled || first == second) progress = false;
      irqstate_t flags = spin_lock_irqsave(&priv->dmalock);
      if (priv->reload_phase != DSI_RELOAD_ACTIVE || priv->reload_error ||
          g_esp_mipi_dsi_frames != initial_irqs ||
          priv->dw_hal.dev->ch[ESP_MIPI_DSI_DMA_CHAN].cfg0.src_multblk_type !=
            DW_GDMA_BLOCK_TRANSFER_RELOAD ||
          priv->dw_hal.dev->ch[ESP_MIPI_DSI_DMA_CHAN].cfg0.dst_multblk_type !=
            DW_GDMA_BLOCK_TRANSFER_RELOAD) progress = false;
      spin_unlock_irqrestore(&priv->dmalock, flags);
    }

  ret = esp_mipi_dsi_reload_end(true, progress);
  printf("[scanout-hold] probe result=%d qualified=%u\n",
         ret ? ret : progress ? 0 : -EIO, g_dsi_reload_qualified);
  return ret ? ret : progress ? 0 : -EIO;
}
