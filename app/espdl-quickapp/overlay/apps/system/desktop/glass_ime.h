/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <lvgl/lvgl.h>
/* Reserve 48 px above the keyboard. Owned by the keyboard's lifetime. */
void glass_ime_attach(lv_obj_t *keyboard, const lv_font_t *font, bool chinese,
                       uint32_t foreground, uint32_t surface, uint32_t accent);
void glass_ime_commit(lv_obj_t *keyboard);
void glass_ime_commit_tree(lv_obj_t *parent);
