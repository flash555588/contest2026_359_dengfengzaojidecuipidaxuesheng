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
static bool camera_reference_ui_controls(lv_obj_t *scene,unsigned style,bool video,bool ready,bool menu,bool filters,
                                         unsigned width,unsigned height,unsigned canvas,unsigned zoom,unsigned aspect)
{
  static lv_image_dsc_t filter[CAMERA_COLOR_STYLE_COUNT],record[2],mode[2],caption[CAMERA_COLOR_STYLE_COUNT];
  if(style>=CAMERA_COLOR_STYLE_COUNT)return false;
  const unsigned ids[]={CAMERA_REFERENCE_FILTER_TILE,CAMERA_REFERENCE_RECORD_TILE,
                        CAMERA_REFERENCE_MODE_TILE,CAMERA_REFERENCE_STYLE_TILE};
  const unsigned guides=CAMERA_REFERENCE_GUIDE_COUNT;
  if(lv_obj_get_child_count(scene)<guides+CAMERA_REFERENCE_TILE_COUNT+6)return false;
  struct camera_reference_rect frame=camera_reference_frame(canvas,width,height,zoom,aspect);
  int w=lv_obj_get_width(scene),h=lv_obj_get_height(scene);
  for(unsigned i=0;i<guides;i++){
    int r[4];camera_reference_guide(frame,i,r);
    lv_obj_t *line=lv_obj_get_child(scene,i);
    lv_obj_set_pos(line,r[0]*w/1024,r[1]*h/600);
    lv_obj_set_size(line,(r[2]*w+1023)/1024,(r[3]*h+599)/600);
  }
  for(unsigned i=0;i<4;i++){
    const struct camera_reference_tile *t=&camera_reference_tiles[ids[i]];
    lv_obj_t *image=lv_obj_get_child(scene,guides+ids[i]);
    if(!lv_obj_check_type(image,&lv_image_class))return false;
    lv_image_dsc_t *d=i==0?&filter[style]:i==1?&record[video]:i==2?&mode[video]:&caption[style];
    *d=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,
       .w=t->w,.h=t->h,.stride=t->w*2},.data_size=t->w*t->h*2,
       .data=(const uint8_t *)camera_reference_control_pixels(ids[i],style,video)};
    lv_image_set_src(image,d);
  }
  lv_obj_t *shutter=lv_obj_get_child(scene,guides+CAMERA_REFERENCE_SHUTTER_TILE);
  if(!lv_obj_check_type(shutter,&lv_image_class))return false;
  lv_obj_set_style_opa(shutter,ready?LV_OPA_COVER:80,0);
  const unsigned filter_index=guides+CAMERA_REFERENCE_TILE_COUNT;
  lv_obj_t *preview=lv_obj_get_child(scene,filter_index);
  for(unsigned i=0;i<CAMERA_COLOR_STYLE_COUNT;i++){
    lv_obj_t *card=lv_obj_get_child(preview,i);
    lv_obj_set_style_border_color(card,lv_color_hex(i==style?0xf13c2e:0x2b2e33),0);
    lv_obj_set_style_border_width(card,i==style?2:1,0);
  }
  lv_obj_t *aspect_group=lv_obj_get_child(scene,filter_index+1);
  lv_obj_t *outline=lv_obj_get_child(aspect_group,0);
  unsigned ah=aspect?camera_aspect_height(32,aspect):20;
  lv_obj_set_pos(outline,12*w/1024,(30-ah/2)*h/600);
  lv_obj_set_size(outline,32*w/1024,ah*h/600);
  for(unsigned i=0;i<4;i++)lv_obj_set_style_bg_color(lv_obj_get_child(aspect_group,i+1),
    lv_color_hex(aspect==i+1?0xf13c2e:0x2b2e33),0);
  for(unsigned i=guides;i<filter_index+6;i++){
    lv_obj_t *child=lv_obj_get_child(scene,i);
    bool hide=i==filter_index+1?filters:i==filter_index?(!filters||menu):
      (menu&&i!=guides+CAMERA_REFERENCE_SHUTTER_TILE)||(filters&&i>filter_index);
    if(hide)lv_obj_add_flag(child,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(child,LV_OBJ_FLAG_HIDDEN);
  }
  return true;
}
static void camera_reference_ui_filter_create(lv_obj_t *root,int w,int h)
{
  lv_obj_t *group=lv_obj_create(root);lv_obj_remove_style_all(group);
  lv_obj_set_pos(group,CAMERA_FILTER_X*w/1024,CAMERA_FILTER_Y*h/600);
  lv_obj_set_size(group,CAMERA_FILTER_W*w/1024,CAMERA_FILTER_H*h/600);
  lv_obj_remove_flag(group,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(group,LV_OBJ_FLAG_HIDDEN);
  for(unsigned i=0;i<CAMERA_COLOR_STYLE_COUNT;i++){
    lv_obj_t *card=lv_obj_create(group);lv_obj_remove_style_all(card);
    lv_obj_set_pos(card,(camera_reference_filter_x(i)-CAMERA_FILTER_X)*w/1024,
                   (camera_reference_filter_y(i)-CAMERA_FILTER_Y)*h/600);
    lv_obj_set_size(card,40*w/1024,40*h/600);
    lv_obj_set_style_bg_color(card,lv_color_hex(camera_reference_filter_colors[i]),0);
    lv_obj_set_style_bg_opa(card,LV_OPA_COVER,0);lv_obj_set_style_radius(card,6,0);
    lv_obj_remove_flag(card,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  }
}
static void camera_reference_ui_create(lv_obj_t *root)
{
  static lv_image_dsc_t images[CAMERA_REFERENCE_TILE_COUNT];
  int w=lv_obj_get_width(root),h=lv_obj_get_height(root);
  struct camera_reference_rect frame=camera_reference_frame(1024,1024,600,100,0);
  for(unsigned i=0;i<CAMERA_REFERENCE_GUIDE_COUNT;i++){
    int r[4];camera_reference_guide(frame,i,r);
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
  camera_reference_ui_filter_create(root,w,h);
  lv_obj_t *aspect=lv_obj_create(root);lv_obj_remove_style_all(aspect);
  lv_obj_set_pos(aspect,800*w/1024,504*h/600);lv_obj_set_size(aspect,56*w/1024,78*h/600);
  lv_obj_remove_flag(aspect,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *outline=lv_obj_create(aspect);lv_obj_remove_style_all(outline);
  lv_obj_set_style_border_color(outline,lv_color_hex(0x9ba0a8),0);lv_obj_set_style_border_width(outline,1,0);
  lv_obj_set_style_radius(outline,2,0);lv_obj_remove_flag(outline,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  for(unsigned i=0;i<4;i++){
    lv_obj_t *dot=lv_obj_create(aspect);lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot,(10+i*10)*w/1024,60*h/600);lv_obj_set_size(dot,4*w/1024,4*h/600);
    lv_obj_set_style_radius(dot,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_opa(dot,LV_OPA_COVER,0);
    lv_obj_remove_flag(dot,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
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
