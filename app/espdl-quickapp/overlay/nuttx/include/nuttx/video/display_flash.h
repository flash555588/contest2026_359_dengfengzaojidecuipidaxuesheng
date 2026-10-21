/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Task context, outside the SDK cache/scheduler guard. */
#ifdef CONFIG_ESPRESSIF_MIPI_DSI
void esp_display_flash_begin(void);
void esp_display_flash_end(void);
#else
static inline void esp_display_flash_begin(void) {}
static inline void esp_display_flash_end(void) {}
#endif
/* Internal-RAM ISR admission; every successful admission must be released. */
bool esp_display_flash_isr_enter(void);
void esp_display_flash_isr_leave(bool admitted);
/* P4 DW-GDMA channel propagation writes, including configuration during Flash. */
void esp_display_flash_intr_update(void *device,unsigned channel,uint32_t mask,bool enable);
#ifdef __cplusplus
}
#endif
