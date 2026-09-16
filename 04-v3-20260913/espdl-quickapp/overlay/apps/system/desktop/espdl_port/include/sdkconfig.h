/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <nuttx/config.h>
#define CONFIG_IDF_TARGET_ESP32P4 1
#define CONFIG_SPIRAM 1
/* P4 PIE and hardware loops are saved by the NuttX extended-context hook. */
#define CONFIG_ESPDL_PORTABLE_KERNELS 0
#if !CONFIG_ARCH_RISCV_INTXCPT_EXTENSIONS
#error "ESP-DL acceleration requires P4 context protection"
#endif
#define CONFIG_SOC_PPA_SUPPORTED 0
#define CONFIG_PPA_ENABLE 0
