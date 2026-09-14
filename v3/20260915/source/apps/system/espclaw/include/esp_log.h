/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CLAW_POSIX_ESP_LOG_H
#define CLAW_POSIX_ESP_LOG_H
#include <syslog.h>
#define ESP_LOG_DEBUG LOG_DEBUG
#define ESP_LOG_WARN LOG_WARNING
#define ESP_LOG_LEVEL(level, tag, fmt, ...) do { \
    int claw_log_priority = (level); \
    if (claw_log_priority != LOG_DEBUG) \
        syslog(claw_log_priority, "%s: " fmt "\n", tag, ##__VA_ARGS__); \
} while (0)
#define ESP_LOGE(tag, fmt, ...) syslog(LOG_ERR, "%s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) syslog(LOG_WARNING, "%s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) syslog(LOG_INFO, "%s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) do { if (0) syslog(LOG_DEBUG, "%s: " fmt, tag, ##__VA_ARGS__); } while (0)
#endif
