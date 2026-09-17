/****************************************************************************
 * apps/system/desktop/pet_lvgl.h
 *
 * Floating desktop overlay for the native pet engine.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#pragma once

#include <stddef.h>
#include <lvgl/lvgl.h>

int pet_lvgl_create(lv_obj_t *parent, const lv_font_t *font_zh);
void pet_lvgl_sync(void);
void pet_lvgl_destroy(void);
bool pet_lvgl_is_open(void);
bool pet_lvgl_uses_sd_sprites(void);
size_t pet_lvgl_sprite_bytes(void);
