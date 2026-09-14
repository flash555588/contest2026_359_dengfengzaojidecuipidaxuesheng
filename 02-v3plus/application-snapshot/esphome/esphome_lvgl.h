/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENVELA_ESPHOME_LVGL_H
#define OPENVELA_ESPHOME_LVGL_H

#include <lvgl/lvgl.h>

typedef void (*esphome_lvgl_close_cb_t)(void);

/* All entry points belong to the LVGL thread. font_zh is a borrowed tiny TTF
 * font and must outlive the page. The page owns its polling timer and stops
 * the client asynchronously on close or on deletion by its parent.
 */
int esphome_lvgl_create(lv_obj_t *parent, const lv_font_t *font_zh,
                        esphome_lvgl_close_cb_t close_cb);
void esphome_lvgl_close(void);
bool esphome_lvgl_is_open(void);

#endif
