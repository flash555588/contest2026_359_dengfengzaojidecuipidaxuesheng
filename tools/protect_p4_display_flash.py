"""Narrow, idempotent Flash/DSI coordination for the explicitly prepared P4 SDK."""
import hashlib
from pathlib import Path

def once(source,old,new,label):
    if source.count(old)!=1:raise RuntimeError('Unknown display/Flash anchor: '+label)
    return source.replace(old,new,1)

def apply(nuttx):
    root=Path(nuttx);common=root/'arch/risc-v/src/common/espressif'
    sdk=root/'arch/risc-v/src/esp32p4/esp-hal-3rdparty'
    paths=[common/'esp_mipi_dsi.c',common/'esp_spiflash.c',
           sdk/'components/esp_hal_dma/esp32p4/include/hal/dw_gdma_ll.h']
    receipts={};pending={}
    for path in paths:
        source=path.read_text();before=hashlib.sha256(source.encode()).hexdigest()
        if 'ESPCLAW DISPLAY FLASH GUARD' not in source:
            if path.name=='esp_mipi_dsi.c':
                source=once(source,'#include <nuttx/video/camera_timing.h>',
                    '#include <nuttx/video/camera_timing.h>\n/* ESPCLAW DISPLAY FLASH GUARD */\n#include <nuttx/video/display_flash.h>\n#include <nuttx/signal.h>','DSI include')
                source=once(source,'  uint32_t status;','  uint32_t status;\n  bool flash_callbacks;','ISR local')
                source=once(source,'  dev = priv->dw_hal.dev;\n  flags = spin_lock_irqsave(&priv->dmalock);',
                    '  dev = priv->dw_hal.dev;\n  flash_callbacks = esp_display_flash_isr_enter();\n  flags = spin_lock_irqsave(&priv->dmalock);','ISR admission')
                source=once(source,'      next_fb = priv->pending_fb;\n      priv->pending_fb = NULL;',
                    '      if (flash_callbacks)\n        {\n          next_fb = priv->pending_fb;\n          priv->pending_fb = NULL;\n        }','defer pan')
                source=once(source,'      esp_camera_display_frame();',
                    '      esp_camera_display_frame();\n      if (!flash_callbacks) esp_camera_display_guard_frame();','guard frame counter')
                if source.count('      dw_gdma_shared_isr();')!=1 or source.count('  dw_gdma_shared_isr();')!=2:
                    raise RuntimeError('Unknown shared DMA dispatch layout')
                source=once(source,'      dw_gdma_shared_isr();',
                    '      if (flash_callbacks) dw_gdma_shared_isr();','early shared dispatch')
                source=once(source,'#endif\n      return OK;\n    }\n\n  flags = spin_lock_irqsave(&priv->dmalock);',
                    '#endif\n      esp_display_flash_isr_leave(flash_callbacks);\n      return OK;\n    }\n\n  flags = spin_lock_irqsave(&priv->dmalock);','early ISR release')
                source=once(source,'  dw_gdma_shared_isr();\n#endif\n\n  return OK;',
                    '  if (flash_callbacks) dw_gdma_shared_isr();\n#endif\n  esp_display_flash_isr_leave(flash_callbacks);\n  return OK;','normal ISR release')
                old='''      ret = esp_setup_irq(DW_GDMA_INTR_SOURCE,
                          ESP_IRQ_PRIORITY_DEFAULT,
                          ESP_IRQ_TRIGGER_LEVEL,
                          esp_mipi_dsi_dma_isr, priv);'''
                new='''      ret = esp_setup_irq_with_flags(DW_GDMA_INTR_SOURCE,
                          ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM |
                          ESP_INTR_FLAG_INTRDISABLED,
                          esp_mipi_dsi_dma_isr, priv);'''
                source=once(source,old,new,'IRAM IRQ registration')
                anchor='static int esp_mipi_dsi_dma_setup(FAR struct esp_mipi_dsi_priv_s *priv)\n{'
                helper='''/* Cache-on task context only; do not hold SDK/DSI/guard locks here. */
void esp_mipi_dsi_flash_progress(void)
{
  FAR struct esp_mipi_dsi_priv_s *priv = &g_esp_mipi_dsi;
  uint64_t until = esp_camera_timing_now() + 50000;
  for (;;)
    {
      irqstate_t flags = spin_lock_irqsave(&priv->dmalock);
      bool pending = priv->dma_enabled && priv->pending_fb != NULL;
      spin_unlock_irqrestore(&priv->dmalock, flags);
      if (!pending || esp_camera_timing_now() >= until) return;
      nxsig_usleep(1000);
    }
}

'''
                source=once(source,anchor,helper+anchor,'cache-on pan progress')
            elif path.name=='esp_spiflash.c':
                source=once(source,'#include <nuttx/video/camera_timing.h>',
                    '#include <nuttx/video/camera_timing.h>\n/* ESPCLAW DISPLAY FLASH GUARD */\n#include <nuttx/video/display_flash.h>','Flash include')
                for op in ['read','write','erase']:
                    old=f'  int ret=spiflash_direct_{op}_untimed('
                    source=once(source,old,'  esp_display_flash_begin();\n'+old,'Flash begin '+op)
                    old=f'  esp_camera_flash_end(CAMERA_FLASH_{op.upper()},length,started,ret);'
                    source=once(source,old,'  esp_display_flash_end();\n'+old,'Flash end '+op)
            else:
                source=once(source,'static inline void dw_gdma_ll_channel_enable_intr_propagation(',
                    '/* ESPCLAW DISPLAY FLASH GUARD */\n#ifdef __NuttX__\n#include <nuttx/video/display_flash.h>\n#endif\nstatic inline void dw_gdma_ll_channel_enable_intr_propagation(','HAL include')
                old='''    if (en) {
        dev->ch[channel].int_sig_ena0.val |= mask;
    } else {
        dev->ch[channel].int_sig_ena0.val &= ~mask;
    }'''
                new='''#if defined(__NuttX__) && defined(CONFIG_ESPRESSIF_MIPI_DSI)
    esp_display_flash_intr_update(dev, channel, mask, en);
#else
'''+old+'''
#endif'''
                source=once(source,old,new,'HAL channel mask hook')
        # esp_irq.h exposes the allocator wrapper, while flag constants live
        # in the SDK allocator header (not transitively included by it).
        if path.name=='esp_mipi_dsi.c' and '#include "esp_intr_alloc.h"' not in source:
            source=once(source,'#include <nuttx/video/display_flash.h>',
                '#include <nuttx/video/display_flash.h>\n#include "esp_intr_alloc.h"','allocator flags include')
        required={
            'esp_mipi_dsi.c': {
                'ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM':1,
                'flash_callbacks = esp_display_flash_isr_enter();':1,
                'esp_display_flash_isr_leave(flash_callbacks);':2,
                'if (flash_callbacks) dw_gdma_shared_isr();':2,
                'if (!flash_callbacks) esp_camera_display_guard_frame();':1,
                'void esp_mipi_dsi_flash_progress(void)':1,
                '#include "esp_intr_alloc.h"':1,
            },
            'esp_spiflash.c': {'esp_display_flash_begin();':3,'esp_display_flash_end();':3},
            'dw_gdma_ll.h': {'esp_display_flash_intr_update(dev, channel, mask, en);':1},
        }[path.name]
        if any(source.count(anchor)!=count for anchor,count in required.items()):
            raise RuntimeError('Incomplete display/Flash protection: '+str(path))
        pending[path]=source
        receipts[str(path.relative_to(root))]={'before_sha256':before,
            'protected_sha256':hashlib.sha256(source.encode()).hexdigest()}
    # Validate every prepared input before writing any of them.
    for path,source in pending.items():
        if path.read_text()!=source:path.write_text(source)
    return receipts


def restore(nuttx):
    """Remove the rejected cache-off IRQ experiment from a prepared workspace.

    Its leaf-handler tests passed, but board testing faulted in the common IRQ
    return path. Never leave that experiment enabled in normal image builds.
    """
    root=Path(nuttx)
    common=root/'arch/risc-v/src/common/espressif'
    sdk=root/'arch/risc-v/src/esp32p4/esp-hal-3rdparty'
    paths=[common/'esp_mipi_dsi.c',common/'esp_spiflash.c',
           sdk/'components/esp_hal_dma/esp32p4/include/hal/dw_gdma_ll.h']
    pending={};receipts={}
    for path in paths:
        source=path.read_text()
        if 'ESPCLAW DISPLAY FLASH GUARD' in source:
            if path.name=='esp_mipi_dsi.c':
                source=once(source,'/* ESPCLAW DISPLAY FLASH GUARD */\n#include <nuttx/video/display_flash.h>\n#include "esp_intr_alloc.h"\n#include <nuttx/signal.h>\n','','restore DSI includes')
                source=once(source,'  bool flash_callbacks;\n','','restore ISR local')
                source=once(source,'  flash_callbacks = esp_display_flash_isr_enter();\n','','restore ISR admission')
                source=once(source,'      if (!flash_callbacks) esp_camera_display_guard_frame();\n','','restore counter')
                source=once(source,'      if (flash_callbacks)\n        {\n          next_fb = priv->pending_fb;\n          priv->pending_fb = NULL;\n        }',
                    '      next_fb = priv->pending_fb;\n      priv->pending_fb = NULL;','restore pending pan')
                if source.count('if (flash_callbacks) dw_gdma_shared_isr();')!=2:
                    raise RuntimeError('Unknown rejected ISR dispatch shape')
                source=source.replace('if (flash_callbacks) dw_gdma_shared_isr();','dw_gdma_shared_isr();')
                if source.count('esp_display_flash_isr_leave(flash_callbacks);')!=2:
                    raise RuntimeError('Unknown rejected ISR release shape')
                source=source.replace('      esp_display_flash_isr_leave(flash_callbacks);\n','').replace('  esp_display_flash_isr_leave(flash_callbacks);\n','\n')
                source=once(source,'''      ret = esp_setup_irq_with_flags(DW_GDMA_INTR_SOURCE,
                          ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM |
                          ESP_INTR_FLAG_INTRDISABLED,
                          esp_mipi_dsi_dma_isr, priv);''','''      ret = esp_setup_irq(DW_GDMA_INTR_SOURCE,
                          ESP_IRQ_PRIORITY_DEFAULT,
                          ESP_IRQ_TRIGGER_LEVEL,
                          esp_mipi_dsi_dma_isr, priv);''','restore IRQ registration')
                marker='/* Cache-on task context only; do not hold SDK/DSI/guard locks here. */\n'
                start=source.index(marker)
                end=source.index('static int esp_mipi_dsi_dma_setup(',start)
                source=source[:start]+source[end:]
            elif path.name=='esp_spiflash.c':
                source=once(source,'/* ESPCLAW DISPLAY FLASH GUARD */\n#include <nuttx/video/display_flash.h>\n','','restore Flash include')
                if source.count('  esp_display_flash_begin();\n')!=3 or source.count('  esp_display_flash_end();\n')!=3:
                    raise RuntimeError('Unknown rejected Flash wrapper shape')
                source=source.replace('  esp_display_flash_begin();\n','').replace('  esp_display_flash_end();\n','')
            else:
                source=once(source,'/* ESPCLAW DISPLAY FLASH GUARD */\n#ifdef __NuttX__\n#include <nuttx/video/display_flash.h>\n#endif\n','','restore HAL include')
                source=once(source,'''#if defined(__NuttX__) && defined(CONFIG_ESPRESSIF_MIPI_DSI)
    esp_display_flash_intr_update(dev, channel, mask, en);
#else
    if (en) {
        dev->ch[channel].int_sig_ena0.val |= mask;
    } else {
        dev->ch[channel].int_sig_ena0.val &= ~mask;
    }
#endif''','''    if (en) {
        dev->ch[channel].int_sig_ena0.val |= mask;
    } else {
        dev->ch[channel].int_sig_ena0.val &= ~mask;
    }''','restore HAL propagation')
        if 'esp_display_flash_' in source or 'ESPCLAW DISPLAY FLASH GUARD' in source:
            raise RuntimeError('Incomplete rejected IRQ removal: '+str(path))
        if path.name=='esp_mipi_dsi.c' and 'ret = esp_setup_irq(DW_GDMA_INTR_SOURCE,' not in source:
            raise RuntimeError('Unknown normal DSI IRQ registration')
        pending[path]=source
        receipts[str(path.relative_to(root))]=hashlib.sha256(source.encode()).hexdigest()
    for path,source in pending.items():
        if path.read_text()!=source:path.write_text(source)
    return receipts
