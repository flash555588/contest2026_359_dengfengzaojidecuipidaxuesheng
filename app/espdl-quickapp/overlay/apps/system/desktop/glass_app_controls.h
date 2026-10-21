/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <lvgl/lvgl.h>
#include "glass_app_theme.h"

/* Transparent layout containers have no visible background of their own. */
static inline bool glass_app_object_light(lv_obj_t *object)
{
  for(lv_obj_t *parent=object;parent;parent=lv_obj_get_parent(parent))
    if(lv_obj_get_style_bg_opa(parent,LV_PART_MAIN)>=LV_OPA_50)
      return lv_color_brightness(lv_obj_get_style_bg_color(parent,LV_PART_MAIN))>128;
  return true;
}

/* Style existing objects, retaining their callbacks, ownership and geometry. */
static inline void glass_app_control_style(lv_obj_t *obj,uint32_t fill,uint32_t line)
{
  lv_obj_set_style_bg_color(obj,lv_color_hex(fill),0);
  lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
  lv_obj_set_style_radius(obj,GLASS_APP_CONTROL_RADIUS,0);
  lv_obj_set_style_border_width(obj,0,0);
  lv_obj_set_style_border_color(obj,lv_color_hex(line),0);
  lv_obj_set_style_shadow_width(obj,0,0);
  lv_obj_set_style_transform_width(obj,0,LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(obj,0,LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(obj,LV_OPA_80,LV_STATE_PRESSED);
  lv_obj_set_style_opa(obj,LV_OPA_50,LV_STATE_DISABLED);
}
static inline void glass_app_panel_style(lv_obj_t *obj,uint32_t fill,uint32_t line)
{
  lv_obj_set_style_bg_color(obj,lv_color_hex(fill),0);
  lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
  lv_obj_set_style_radius(obj,GLASS_APP_CARD_RADIUS,0);
  lv_obj_set_style_border_width(obj,1,0);
  lv_obj_set_style_border_color(obj,lv_color_hex(line),0);
  lv_obj_set_style_border_opa(obj,LV_OPA_COVER,0);
  lv_obj_set_style_shadow_width(obj,0,0);
}
static inline void glass_app_input_style(lv_obj_t *obj,struct glass_app_palette p)
{
  glass_app_panel_style(obj,p.soft,p.line);
  lv_obj_set_style_radius(obj,GLASS_APP_CONTROL_RADIUS,0);
  lv_obj_set_style_text_color(obj,lv_color_hex(p.ink),0);
  lv_obj_set_style_border_color(obj,lv_color_hex(p.accent),LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(obj,2,LV_STATE_FOCUSED);
}
