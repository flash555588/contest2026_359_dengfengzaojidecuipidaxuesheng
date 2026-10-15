/* SPDX-License-Identifier: Apache-2.0 */
#include "ha_color_wheel.inc"
/* A shared immutable image in flash; one LVGL object, no bitmap heap per page. */
static const lv_image_dsc_t qpk_color_wheel_image = {
  .header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565A8,
           .w=160,.h=160,.stride=320},
  .data_size=sizeof(qpk_color_wheel_pixels),.data=qpk_color_wheel_pixels
};
static int qpk_color_wheel_resize(lv_obj_t *root,int width,int height)
{
  if(lv_obj_get_user_data(root)!=(void *)&qpk_color_wheel_image)return 0;
  if(width!=height||width<64||width>256)return -1;
  lv_obj_set_size(root,width,height);
  lv_image_set_scale(lv_obj_get_child(root,0),width*256/160);
  return 1;
}
static JSValue js_ui_color_wheel(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  (void)self;
  int x=qpk_arg_int(c,argc,argv,0,0),y=qpk_arg_int(c,argc,argv,1,0);
  int size=qpk_arg_int(c,argc,argv,2,160);
  if(size<64||size>256)return JS_ThrowRangeError(c,"invalid color wheel diameter");
  lv_obj_t *root=lv_obj_create(g_qpk.root);
  if(!root)return JS_ThrowOutOfMemory(c);
  lv_obj_remove_style_all(root);lv_obj_set_pos(root,x,y);lv_obj_set_size(root,size,size);
  lv_obj_set_user_data(root,(void *)&qpk_color_wheel_image);
  lv_obj_remove_flag(root,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *image=lv_image_create(root);
  if(!image){lv_obj_delete(root);return JS_ThrowOutOfMemory(c);}
  lv_image_set_src(image,&qpk_color_wheel_image);lv_image_set_pivot(image,0,0);
  lv_image_set_scale(image,size*256/160);lv_image_set_antialias(image,true);
  lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  int h=qpk_add_widget(root,QPK_WIDGET_PANEL);
  if(!h){lv_obj_delete(root);return JS_ThrowOutOfMemory(c);}
  return JS_NewInt32(c,h);
}
