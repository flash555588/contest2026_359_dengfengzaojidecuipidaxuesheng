/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int64_t qpk_dl_timer_us(void);
#ifdef __cplusplus
}
#endif
#define esp_timer_get_time qpk_dl_timer_us
