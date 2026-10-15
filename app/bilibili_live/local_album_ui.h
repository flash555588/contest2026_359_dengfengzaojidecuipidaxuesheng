/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <lvgl/lvgl.h>
void local_album_show(lv_obj_t *parent,const lv_font_t *font);
void local_album_open_path(const char *path);

void local_album_view(lv_obj_t *parent,const lv_font_t *font,const char *path,lv_event_cb_t back);
