/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LIVE_PLATFORM_H
#define LIVE_PLATFORM_H
#include "bili_api.h"
#include <stdatomic.h>
#include <stdbool.h>
enum live_platform { LIVE_BILI, LIVE_HUYA, LIVE_CC, LIVE_PLATFORM_COUNT };
const char *live_platform_name(int platform);
int live_resolve(int platform, const char *room, const char *cookie,
                 atomic_bool *cancel, struct bili_streams *out);
int live_parse_huya(const char *json, const char *room, struct bili_streams *out);
int live_parse_cc(const char *json, struct bili_streams *out);
#endif
