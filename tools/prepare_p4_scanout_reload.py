"""Add cache-on DMA reload handshakes to the prepared DSI/Flash drivers."""
import hashlib
from pathlib import Path
from protect_p4_display_flash import once


def apply(nuttx):
    folder=Path(nuttx)/'arch/risc-v/src/common/espressif'
    receipts={};pending={}
    for name in ['esp_mipi_dsi.c','esp_spiflash.c']:
        path=folder/name;source=path.read_text()
        if 'ESPCLAW SCANOUT RELOAD' not in source:
            if name=='esp_mipi_dsi.c':
                source=once(source,'  sem_t fb_done;', '''  sem_t fb_done;
#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
  sem_t reload_done;
  unsigned reload_phase;
  uint64_t reload_started;
  uint32_t reload_frame_us;
  uint32_t reload_status_mask;
  bool reload_error;
#endif''','reload state')
                source=once(source,'  .fb_done = SEM_INITIALIZER(0),','''  .fb_done = SEM_INITIALIZER(0),
#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
  .reload_done = SEM_INITIALIZER(0),
#endif''','reload semaphore')
                source=once(source,'volatile uint32_t g_esp_mipi_dsi_frames;','''volatile uint32_t g_esp_mipi_dsi_frames;
/* ESPCLAW SCANOUT RELOAD */
#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
#include "esp_mipi_dsi_reload.inc"
#endif''','reload implementation')
                source=once(source,'  bool frame_swapped;','''  bool frame_swapped;
#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
  bool reload_signal = false;
#endif''','reload IRQ signal')
                source=once(source,'''      next_fb = priv->pending_fb;
      priv->pending_fb = NULL;''','''#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
      if (priv->reload_phase != DSI_RELOAD_REQUESTED)
#endif
        {
          next_fb = priv->pending_fb;
          priv->pending_fb = NULL;
        }''','pending preservation')
                source=once(source,'''      esp_mipi_dsi_dma_restart(priv);
    }

  spin_unlock_irqrestore(&priv->dmalock, flags);

  if (frame_swapped)''','''#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
      if (priv->reload_phase == DSI_RELOAD_ACTIVE)
        {
          /* An autonomous stream must not need another completion IRQ. */
          priv->reload_error = true;
          g_dsi_reload_qualified = false;
          priv->reload_phase = DSI_RELOAD_ENDING;
        }
      if (priv->reload_phase == DSI_RELOAD_REQUESTED)
        {
          esp_mipi_dsi_reload_start(priv);
          reload_signal = true;
        }
      else
        {
          dw_gdma_ll_channel_set_src_multi_block_type(dev,
            ESP_MIPI_DSI_DMA_CHAN, DW_GDMA_BLOCK_TRANSFER_LIST);
          dw_gdma_ll_channel_set_dst_multi_block_type(dev,
            ESP_MIPI_DSI_DMA_CHAN, DW_GDMA_BLOCK_TRANSFER_LIST);
          if (priv->reload_phase == DSI_RELOAD_ENDING)
            {
              dev->ch[ESP_MIPI_DSI_DMA_CHAN].int_st_ena0.val =
                priv->reload_status_mask;
              priv->reload_phase = DSI_RELOAD_IDLE;
              reload_signal = true;
            }
          esp_mipi_dsi_dma_restart(priv);
        }
#else
      esp_mipi_dsi_dma_restart(priv);
#endif
    }

  spin_unlock_irqrestore(&priv->dmalock, flags);
#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
  if (reload_signal) nxsem_post(&priv->reload_done);
#endif

  if (frame_swapped)''','reload ISR handoff')
                source=once(source,'  priv->dpi_h_res = cfg->h_res;','''#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
  uint64_t htotal = (uint64_t)cfg->h_res + cfg->hsync_pulse_width +
                    cfg->hsync_back_porch + cfg->hsync_front_porch;
  uint64_t vtotal = (uint64_t)cfg->v_res + cfg->vsync_pulse_width +
                    cfg->vsync_back_porch + cfg->vsync_front_porch;
  priv->reload_frame_us = (htotal * vtotal * dpi_div * 1000000 +
                          dpi_clk_src_freq_hz - 1) / dpi_clk_src_freq_hz;
  g_dsi_reload_qualified = false;
#endif
  priv->dpi_h_res = cfg->h_res;''','actual frame duration')
                # Serialize lifecycle and page admission before their timeouts.
                entries=[('esp_mipi_dsi_swap_framebuffer','FAR void *fb, uint32_t timeout_ms','fb, timeout_ms'),
                         ('esp_mipi_dsi_video_stop','void',''),
                         ('esp_mipi_dsi_video_start','void',''),
                         ('esp_mipi_dsi_bind_framebuffer','FAR void *fb, size_t fb_size,\n                                  uint16_t h_res, uint16_t v_res,\n                                  uint8_t bpp','fb, fb_size, h_res, v_res, bpp'),
                         ('esp_mipi_dsi_configure_dpi','FAR const struct esp_mipi_dsi_dpi_config_s *cfg','cfg')]
                # The prepared baseline's DPI type is checked below rather
                # than guessed if its public signature differs.
                for function,args,call in entries:
                    anchor='int '+function+'('
                    source=once(source,anchor,'static int '+function+'_ungated(',function+' admission')
                    if function=='esp_mipi_dsi_configure_dpi':
                        start=source.index('static int '+function+'_ungated(')
                        args=source[start:].split('(',1)[1].split(')',1)[0]
                    admission='' if function=='esp_mipi_dsi_video_stop' else '''
  if (g_esp_mipi_dsi.reload_phase != DSI_RELOAD_IDLE) {
    nxmutex_unlock(&g_dsi_flash_lock);
    return -EBUSY;
  }
'''
                    source+='''
int '''+function+'('+args+''')
{
#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
  int ret = nxmutex_lock(&g_dsi_flash_lock);
  if (ret < 0) return ret;
'''+admission+'''  ret = '''+function+'_ungated('+call+''');
  nxmutex_unlock(&g_dsi_flash_lock);
  return ret;
#else
  return '''+function+'_ungated('+call+''');
#endif
}
'''
            else:
                source=once(source,'#include <nuttx/config.h>','''#include <nuttx/config.h>
/* ESPCLAW SCANOUT RELOAD */
#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
extern int esp_mipi_dsi_flash_begin(void);
extern int esp_mipi_dsi_flash_end(void);
#endif''','Flash reload declarations')
                for op in ['write','erase']:
                    call='address, '+('buffer, ' if op=='write' else '')+'length'
                    old='  int ret=spiflash_direct_'+op+'_untimed('+call+');'
                    new='''#ifdef CONFIG_ESP32P4_DSI_FLASH_RELOAD
  int held=esp_mipi_dsi_flash_begin();
  int ret=held<0 ? held : spiflash_direct_'''+op+'_untimed('+call+''');
  if(held>0) {
    int resumed=esp_mipi_dsi_flash_end();
    if(!ret&&resumed<0)ret=resumed;
  }
#else
'''+old+'''
#endif'''
                    source=once(source,old,new,op+' hold wrapper')
        if source.count('ESPCLAW SCANOUT RELOAD')!=1:
            raise RuntimeError('Incomplete scanout reload patch: '+name)
        pending[path]=source
        receipts[name]=hashlib.sha256(source.encode()).hexdigest()
    for path,source in pending.items():
        if path.read_text()!=source:path.write_text(source)
    return receipts
