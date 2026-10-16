/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <lvgl/lvgl.h>
/* Optional trusted presentation hooks; independent runtime hosts may omit them.
 * Geometry interpolation is opt-in and restricted to decorative objects. */
void desktop_motion_button(lv_obj_t *button) __attribute__((weak));
bool desktop_motion_geometry(lv_obj_t *obj,int x,int y,int w,int h,unsigned mask,bool animate) __attribute__((weak));

bool desktop_motion_color(lv_obj_t *,uint32_t color,bool animate) __attribute__((weak));

void desktop_motion_present(lv_obj_t **objects,unsigned count,int offset,bool horizontal) __attribute__((weak));
bool desktop_motion_range(lv_obj_t *,int stop,uint32_t accent,uint32_t background,bool animate) __attribute__((weak));
bool desktop_motion_range_linked(lv_obj_t *,lv_obj_t *,int stop,uint32_t accent,uint32_t background,bool animate) __attribute__((weak));
