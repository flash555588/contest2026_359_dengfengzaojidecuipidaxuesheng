/****************************************************************************
 * arch/risc-v/src/common/espressif/esp_mipi_csi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_RISCV_SRC_COMMON_ESPRESSIF_ESP_MIPI_CSI_H
#define __ARCH_RISCV_SRC_COMMON_ESPRESSIF_ESP_MIPI_CSI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct imgdata_s;

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

FAR struct imgdata_s *esp_mipi_csi_initialize(void);
void esp_mipi_csi_dump_status(void);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_RISCV_SRC_COMMON_ESPRESSIF_ESP_MIPI_CSI_H */
