/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_SD_FONT_H
#define GLASS_SD_FONT_H
#include <lvgl/lvgl.h>
#include "glass_sd_font_asset.h"
/* Start checksum work off the UI thread. All other calls are UI-thread only. */
void glass_sd_font_start(void);
/* 1: new fonts ready; -1: withdraw fallbacks after IO failure; 0: unchanged. */
int glass_sd_font_poll(void);
lv_font_t *glass_sd_font_get(unsigned pixels);
void glass_sd_font_status(int *state,int *error);
#endif
