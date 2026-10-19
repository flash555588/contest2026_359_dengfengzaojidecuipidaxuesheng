/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "camera_reference_controls.h"

#include "camera_reference_layout.h"

static const lv_image_dsc_t *camera_reference_zoom_image(int active,int index)
{
  static lv_image_dsc_t images[2][4];
  images[active][index]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,
    .cf=LV_COLOR_FORMAT_RGB565,.w=48,.h=48,.stride=96},
    .data_size=4608,.data=(const uint8_t *)camera_reference_zoom_pixels[active][index]};
  return &images[active][index];
}
static bool camera_reference_ui_zoom(lv_obj_t *scene,int selected)
{
  if(lv_obj_get_child_count(scene)<4)return false;
  for(int row=0;row<4;row++){
    lv_obj_t *image=lv_obj_get_child(scene,-4+row);
    if(!lv_obj_check_type(image,&lv_image_class))return false;
    lv_image_set_src(image,camera_reference_zoom_image(3-row==selected,3-row));
  }
  return true;
}
static bool camera_reference_ui_controls(lv_obj_t *scene,unsigned style,bool video)
{
  static lv_image_dsc_t filter[3],record[2];
  const unsigned ids[]={CAMERA_REFERENCE_FILTER_TILE,CAMERA_REFERENCE_RECORD_TILE};
  const unsigned guides=sizeof(camera_reference_guides)/sizeof(camera_reference_guides[0]);
  if(lv_obj_get_child_count(scene)<guides+CAMERA_REFERENCE_TILE_COUNT+4)return false;
  for(unsigned i=0;i<2;i++){
    const struct camera_reference_tile *t=&camera_reference_tiles[ids[i]];
    lv_obj_t *image=lv_obj_get_child(scene,guides+ids[i]);
    if(!lv_obj_check_type(image,&lv_image_class))return false;
    lv_image_dsc_t *d=i?&record[video]:&filter[style];
    *d=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,
       .w=t->w,.h=t->h,.stride=t->w*2},.data_size=t->w*t->h*2,
       .data=(const uint8_t *)camera_reference_control_pixels(ids[i],style,video)};
    lv_image_set_src(image,d);
  }
  return true;
}
static void camera_reference_ui_create(lv_obj_t *root)
{
  static lv_image_dsc_t images[CAMERA_REFERENCE_TILE_COUNT];
  int w=lv_obj_get_width(root),h=lv_obj_get_height(root);
  for(unsigned i=0;i<sizeof(camera_reference_guides)/sizeof(camera_reference_guides[0]);i++){
    const int *r=camera_reference_guides[i];
    lv_obj_t *line=lv_obj_create(root);lv_obj_remove_style_all(line);
    lv_obj_remove_flag(line,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(line,r[0]*w/1024,r[1]*h/600);
    lv_obj_set_size(line,(r[2]*w+1023)/1024,(r[3]*h+599)/600);
    lv_obj_set_style_bg_color(line,lv_color_hex(0x676268),0);
    lv_obj_set_style_bg_opa(line,LV_OPA_COVER,0);
  }
  for(unsigned i=0;i<CAMERA_REFERENCE_TILE_COUNT;i++){
    const struct camera_reference_tile *t=&camera_reference_tiles[i];
    images[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,
      .cf=LV_COLOR_FORMAT_RGB565,.w=t->w,.h=t->h,.stride=t->w*2},
      .data_size=t->w*t->h*2,.data=(const uint8_t *)t->pixels};
    lv_obj_t *sprite=lv_image_create(root);lv_image_set_src(sprite,&images[i]);
    lv_image_set_pivot(sprite,0,0);
    lv_image_set_scale_x(sprite,w*256/1024);lv_image_set_scale_y(sprite,h*256/600);
    lv_obj_set_pos(sprite,t->x*w/1024,t->y*h/600);
    lv_obj_remove_flag(sprite,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  }
  for(int row=0;row<4;row++){
    lv_obj_t *sprite=lv_image_create(root);
    lv_image_set_src(sprite,camera_reference_zoom_image(row==3,3-row));
    lv_image_set_pivot(sprite,0,0);
    lv_image_set_scale_x(sprite,w*256/1024);lv_image_set_scale_y(sprite,h*256/600);
    lv_obj_set_pos(sprite,708*w/1024,(180+row*64)*h/600);
    lv_obj_remove_flag(sprite,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  }
}
