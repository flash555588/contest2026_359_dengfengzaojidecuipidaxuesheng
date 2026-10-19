/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <lvgl/lvgl.h>

/* LVGL visibility checks flags/parents, not opaque siblings above a page.
 * Only a solid, square, full-display top-layer cover can suppress painting.
 * Partial dialogs, scrims and moving transitions stay conservative. */
static inline bool glass_ui_covered(lv_obj_t *object)
{
  if(!object)return false;
  lv_display_t *display=lv_obj_get_display(object);
  lv_obj_t *top=lv_display_get_layer_top(display),*branch=object;
  if(lv_obj_get_style_opa(top,0)!=LV_OPA_COVER||
     lv_obj_get_style_opa_layered(top,0)!=LV_OPA_COVER)return false;
  while(lv_obj_get_parent(branch)&&lv_obj_get_parent(branch)!=top)
    branch=lv_obj_get_parent(branch);
  if(branch==lv_display_get_layer_sys(display))return false;
  bool in_top=lv_obj_get_parent(branch)==top;
  int width=lv_display_get_horizontal_resolution(display);
  int height=lv_display_get_vertical_resolution(display);
  for(int i=(int)lv_obj_get_child_count(top)-1;i>=0;i--){
    lv_obj_t *cover=lv_obj_get_child(top,i);
    if(in_top&&cover==branch)break;
    if(!lv_obj_is_visible(cover)||lv_obj_get_style_opa(cover,0)!=LV_OPA_COVER||
       lv_obj_get_style_bg_opa(cover,0)!=LV_OPA_COVER||
       lv_obj_get_style_radius(cover,0)!=0||
       lv_obj_get_style_bg_grad_dir(cover,0)!=LV_GRAD_DIR_NONE||
       lv_obj_get_style_opa_layered(cover,0)!=LV_OPA_COVER||
       lv_obj_get_style_blend_mode(cover,0)!=LV_BLEND_MODE_NORMAL||
       lv_obj_get_style_translate_x(cover,0)||lv_obj_get_style_translate_y(cover,0)||
       lv_obj_get_style_transform_rotation(cover,0)||
       lv_obj_get_style_transform_skew_x(cover,0)||lv_obj_get_style_transform_skew_y(cover,0)||
       lv_obj_get_style_transform_scale_x(cover,0)!=256||
       lv_obj_get_style_transform_scale_y(cover,0)!=256)continue;
    lv_area_t area;lv_obj_get_coords(cover,&area);
    if(area.x1<=0&&area.y1<=0&&area.x2>=width-1&&area.y2>=height-1)return true;
  }
  return false;
}
static inline bool glass_ui_can_paint(lv_obj_t *object)
{
  if(!object)return false;
  lv_obj_t *root=object;
  for(;;){
    if(lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN))return false;
    lv_obj_t *parent=lv_obj_get_parent(root);if(!parent)break;root=parent;
  }
  lv_display_t *display=lv_obj_get_display(object);
  if(root!=lv_display_get_screen_active(display)&&root!=lv_display_get_layer_top(display)&&
     root!=lv_display_get_layer_sys(display))return false;
  /* New pages have no layout yet. Do not delay their initial text until the
   * next polling interval merely because LVGL's old bounds are empty. */
  return !glass_ui_covered(object);
}
