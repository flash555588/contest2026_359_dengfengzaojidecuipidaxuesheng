/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <lvgl/lvgl.h>
void local_video_show(lv_obj_t *, const lv_font_t *);
void local_video_stop(void);
void local_video_play_path(const char *);

void local_video_view(lv_obj_t *parent,const lv_font_t *font,const char *path,lv_event_cb_t back);
