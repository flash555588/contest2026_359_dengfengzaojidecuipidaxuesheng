/* SPDX-License-Identifier: Apache-2.0
 * ESP32-P4 dedicated HS/UTMI host; independent of the console FS PHY.
 */
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/arch.h>
#include "esp_irq.h"
#include "hal/usb_utmi_ll.h"
#include "soc/lp_system_struct.h"
#include "soc/interrupts.h"
#include "esp_cache.h"
#include "usbh_core.h"
#include "usb_dwc2_param.h"

static int g_phy_ready;
static int g_cpuint = -1;
static int camera_usb_irq(int irq, void *context, void *arg)
{
  /* Channel lookup and completion must serialize with STOP on the other
   * CPU, not just the UVC callback. The NuttX IRQ lock is recursive. */
  irqstate_t flags = enter_critical_section();
  USBH_IRQHandler(0);
  leave_critical_section(flags);
  return 0;
}
int camera_usb_phy_ready(void) { return g_phy_ready; }
void usb_hc_low_level_init(struct usbh_bus *bus)
{
  irqstate_t flags = enter_critical_section();
  _usb_utmi_ll_enable_bus_clock(true);
  _usb_utmi_ll_reset_register();
  usb_utmi_ll_enable_precise_detection(true);
  usb_utmi_ll_configure_ls(&USB_UTMI, true);
  /* P4 v3 does not connect DWC2 pulldown outputs to the UTMI PHY. */
  LP_SYS.hp_usb_otghs_phy_ctrl.hp_utmiotg_dppulldown = 1;
  LP_SYS.hp_usb_otghs_phy_ctrl.hp_utmiotg_dmpulldown = 1;
  leave_critical_section(flags);
  g_cpuint = esp_setup_irq(ETS_USB_OTG_INTR_SOURCE,
                           ESP_IRQ_PRIORITY_DEFAULT, ESP_IRQ_TRIGGER_LEVEL,
                           camera_usb_irq, NULL);
  if (g_cpuint < 0) return;
  int irq = ESP_SOURCE2IRQ(ETS_USB_OTG_INTR_SOURCE);
  g_phy_ready = 1;
  up_enable_irq(irq);
}
void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
  if (g_cpuint >= 0)
    {
      int irq = ESP_SOURCE2IRQ(ETS_USB_OTG_INTR_SOURCE);
      up_disable_irq(irq);
      esp_teardown_irq(ETS_USB_OTG_INTR_SOURCE, g_cpuint);
      g_cpuint = -1;
    }
  g_phy_ready = 0;
}
void dwc2_get_user_params(uint32_t base, struct dwc2_user_params *p)
{
  memset(p, 0, sizeof(*p));
  p->phy_type = DWC2_PHY_TYPE_PARAM_UTMI;
  /* P4's internal HS PHY is wired as 16-bit UTMI (30 MHz). */
  p->phy_utmi_width = 16;
  p->host_rx_fifo_size = 640;
  p->host_nperio_tx_fifo_size = 128;
  p->host_perio_tx_fifo_size = 128;
}
void usb_dcache_clean(uintptr_t addr, size_t size)
{
  if (size) esp_cache_msync((void *)addr, size,
    ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}
void usb_dcache_invalidate(uintptr_t addr, size_t size)
{
  if (size) esp_cache_msync((void *)addr, size,
    ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}
void usb_dcache_flush(uintptr_t addr, size_t size)
{
  if (size) esp_cache_msync((void *)addr, size, ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}
