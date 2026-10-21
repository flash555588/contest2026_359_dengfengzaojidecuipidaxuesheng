/****************************************************************************
 * arch/risc-v/src/esp32p4/esp32p4_display_flash.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/spinlock.h>
#include <nuttx/video/display_flash.h>

#include <debug.h>
#include <errno.h>

#include "esp_attr.h"
#include "hal/dw_gdma_ll.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* This singleton protects P4's one DW-GDMA controller. Internal BSS only:
 * every ISR-side callee and datum must survive cache-off. The prepared SDK
 * keeps the controller clock on after the first channel allocation.
 */

static spinlock_t g_flash_lock = SP_UNLOCKED;
static dw_gdma_dev_t *g_flash_hw;
static unsigned int g_flash_depth;
static unsigned int g_unsafe_readers;
static uint32_t g_desired[DW_GDMA_LL_CHANNELS_PER_GROUP];

extern void esp_mipi_dsi_flash_progress(void);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void IRAM_ATTR esp_display_flash_intr_update(void *device,
                                           unsigned int channel,
                                           uint32_t mask, bool enable)
{
  dw_gdma_dev_t *hw = device;
  irqstate_t flags;
  unsigned int i;

  if (channel >= DW_GDMA_LL_CHANNELS_PER_GROUP)
    {
      return;
    }

  flags = spin_lock_irqsave(&g_flash_lock);

  /* The first LL access follows clock/reset. Flash before that only changes
   * depth and must never read an unclocked peripheral.
   */

  if (g_flash_hw == NULL)
    {
      g_flash_hw = hw;
      for (i = 0; i < DW_GDMA_LL_CHANNELS_PER_GROUP; i++)
        {
          g_desired[i] = hw->ch[i].int_sig_ena0.val;
          if (g_flash_depth != 0 && i != 0)
            {
              hw->ch[i].int_sig_ena0.val = 0;
            }
        }
    }

  if (hw != g_flash_hw)
    {
      spin_unlock_irqrestore(&g_flash_lock, flags);
      return;
    }

  if (enable)
    {
      g_desired[channel] |= mask;
    }
  else
    {
      g_desired[channel] &= ~mask;
    }

  /* Direct write avoids recursion through the patched LL helper. Keep
   * pending status latched and preserve configuration changes during Flash.
   * Channel 0 is reserved for DSI in this prepared platform.
   */

  hw->ch[channel].int_sig_ena0.val = g_flash_depth != 0 && channel != 0 ?
                                  0 : g_desired[channel];
  spin_unlock_irqrestore(&g_flash_lock, flags);
}

bool IRAM_ATTR esp_display_flash_isr_enter(void)
{
  irqstate_t flags;
  bool admitted;

  flags = spin_lock_irqsave(&g_flash_lock);
  admitted = g_flash_depth == 0;
  if (admitted)
    {
      g_unsafe_readers++;
    }

  spin_unlock_irqrestore(&g_flash_lock, flags);
  return admitted;
}

void IRAM_ATTR esp_display_flash_isr_leave(bool admitted)
{
  irqstate_t flags;

  if (!admitted)
    {
      return;
    }

  flags = spin_lock_irqsave(&g_flash_lock);
  if (g_unsafe_readers != 0)
    {
      g_unsafe_readers--;
    }

  spin_unlock_irqrestore(&g_flash_lock, flags);
}

void esp_display_flash_begin(void)
{
  irqstate_t flags;
  unsigned int i;
  bool idle;

  DEBUGASSERT(!up_interrupt_context());
  flags = spin_lock_irqsave(&g_flash_lock);
  if (g_flash_depth++ == 0 && g_flash_hw != NULL)
    {
      for (i = 1; i < DW_GDMA_LL_CHANNELS_PER_GROUP; i++)
        {
          g_flash_hw->ch[i].int_sig_ena0.val = 0;
        }
    }

  spin_unlock_irqrestore(&g_flash_lock, flags);

  /* Gate new unsafe callbacks before waiting. Never wait with a lock held
   * or IRQs disabled: an admitted ISR must finish before the SDK disables
   * cache/scheduling or parks the other CPU.
   */

  while (true)
    {
      flags = spin_lock_irqsave(&g_flash_lock);
      idle = g_unsafe_readers == 0;
      spin_unlock_irqrestore(&g_flash_lock, flags);
      if (idle)
        {
          break;
        }

      up_udelay(1);
    }
}

void esp_display_flash_end(void)
{
  int saved_errno = errno;
  irqstate_t flags;
  unsigned int i;
  bool released;

  flags = spin_lock_irqsave(&g_flash_lock);
  DEBUGASSERT(g_flash_depth > 0);
  released = g_flash_depth != 0 && --g_flash_depth == 0;
  if (released && g_flash_hw != NULL)
    {
      for (i = 1; i < DW_GDMA_LL_CHANNELS_PER_GROUP; i++)
        {
          g_flash_hw->ch[i].int_sig_ena0.val = g_desired[i];
        }
    }

  spin_unlock_irqrestore(&g_flash_lock, flags);

  /* Cache and SDK locks are restored. Pending pan/CSI work can progress
   * between Flash operations. No guard/DSI lock is held while waiting.
   */

  if (released)
    {
      esp_mipi_dsi_flash_progress();
    }

  errno = saved_errno;
}

