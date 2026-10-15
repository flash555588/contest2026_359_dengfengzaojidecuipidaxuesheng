/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdio.h>
#define ESP_LOGE(tag,fmt,...) fprintf(stderr,"speech/%s: " fmt "\n",tag,##__VA_ARGS__)
#define ESP_LOGW(tag,fmt,...) ESP_LOGE(tag,fmt,##__VA_ARGS__)
#define ESP_LOGI(tag,fmt,...) do {(void)(tag);} while(0)
#define ESP_LOGD(tag,fmt,...) do {(void)(tag);} while(0)
#define ESP_LOGV(tag,fmt,...) do {(void)(tag);} while(0)
