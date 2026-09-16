/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
#include "esp_heap_caps.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void qpk_dl_log(int level, const char *tag, const char *format, ...);
uint32_t esp_log_timestamp(void);
#ifdef __cplusplus
}
#endif
#define ESP_LOGE(tag, fmt, ...) qpk_dl_log(1, tag, fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) qpk_dl_log(2, tag, fmt "\n", ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) qpk_dl_log(3, tag, fmt "\n", ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) do {} while (0)
#define ESP_LOGV(tag, fmt, ...) do {} while (0)
#define ESP_LOG_BUFFER_HEX(tag, data, len) do {} while (0)
#define ESP_EARLY_LOGE ESP_LOGE
#define ESP_EARLY_LOGW ESP_LOGW
#define ESP_EARLY_LOGI ESP_LOGI
