/* SPDX-License-Identifier: Apache-2.0 */
/* Shared native styling for the reviewed settings, performance and weather. */
static const lv_font_t *home_font(int size);
static lv_obj_t *home_text(lv_obj_t *,const char *,int,int,int,int,uint32_t);
static lv_obj_t *home_icon(lv_obj_t *,const char *,int,int,int,uint32_t);
static uint32_t detail_canvas(void){return g_desktop.light_theme?0xf4f5f8:0x15171e;}
static uint32_t detail_paper(void){return g_desktop.light_theme?0xffffff:0x22252f;}
static uint32_t detail_soft(void){return g_desktop.light_theme?0xf3f2f7:0x2e303d;}
static uint32_t detail_line(void){return g_desktop.light_theme?0xe8e9ef:0x383b48;}
static uint32_t detail_ink(void){return g_desktop.light_theme?0x292c38:0xeeeef5;}
static uint32_t detail_muted(void){return g_desktop.light_theme?0x808491:0xa2a7b7;}
static uint32_t detail_accent(void){return g_desktop.light_theme?0x77658f:0xc1aedb;}
#include "glass_sidebar.h"
static void detail_page(lv_obj_t *card)
{
  lv_obj_clean(card);lv_obj_remove_style_all(card);
  lv_obj_set_pos(card,0,0);lv_obj_set_size(card,1024,GLASS_CONTENT_HEIGHT);
  lv_obj_remove_flag(card,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card,lv_color_hex(detail_canvas()),0);
  lv_obj_set_style_bg_opa(card,LV_OPA_COVER,0);
}
static lv_obj_t *detail_tile(lv_obj_t *parent,int x,int y,int w,int h)
{
  lv_obj_t *tile=glass_box(parent,x,y,w,h,detail_paper(),18);
  lv_obj_set_style_border_width(tile,1,0);
  lv_obj_set_style_border_color(tile,lv_color_hex(detail_line()),0);
  lv_obj_set_style_shadow_width(tile,12,0);
  lv_obj_set_style_shadow_ofs_y(tile,4,0);
  lv_obj_set_style_shadow_opa(tile,g_desktop.light_theme?8:0,0);
  lv_obj_set_style_shadow_color(tile,lv_color_hex(0x18243c),0);
  return tile;
}
static lv_obj_t *detail_text(lv_obj_t *parent,const char *text,int x,int y,int w,int size,uint32_t color)
{return home_text(parent,text,x,y,w,size,color);}
static lv_obj_t *detail_button(lv_obj_t *parent,const char *text,int x,int y,int w,lv_event_cb_t cb,void *data)
{
  lv_obj_t *button=glass_button(parent,text,x,y,w,detail_soft(),cb,data);
  lv_obj_set_height(button,44);lv_obj_set_style_radius(button,12,0);
  lv_obj_set_style_border_width(button,0,0);
  lv_obj_set_style_text_font(lv_obj_get_child(button,0),home_font(14),0);
  lv_obj_set_style_text_color(lv_obj_get_child(button,0),lv_color_hex(detail_ink()),0);
  return button;
}
static lv_obj_t *detail_nav(lv_obj_t *parent,const char *text,const char *icon,int y,int w,lv_event_cb_t cb,void *data)
{
  lv_obj_t *row=detail_button(parent,"",20,y,w,cb,data);
  home_icon(row,icon,w==GLASS_SIDEBAR_ITEM_WIDTH?12:14,12,20,detail_accent());
  detail_text(row,text,w==GLASS_SIDEBAR_ITEM_WIDTH?42:46,12,w==GLASS_SIDEBAR_ITEM_WIDTH?w-50:w-80,14,detail_ink());
  if(w!=GLASS_SIDEBAR_ITEM_WIDTH)home_icon(row,LV_SYMBOL_RIGHT,w-30,14,16,detail_muted());
  return row;
}
static void detail_sidebar(lv_obj_t *card,const char *name,const char *icon,lv_event_cb_t back)
{
  lv_obj_t *sidebar=glass_box(card,0,0,GLASS_SIDEBAR_WIDTH,GLASS_CONTENT_HEIGHT,detail_paper(),0);
  glass_sidebar_surface(sidebar);
  glass_box(card,GLASS_SIDEBAR_WIDTH-1,0,1,GLASS_CONTENT_HEIGHT,detail_line(),0);
  home_icon(card,icon,24,30,24,detail_accent());
  detail_text(card,name,58,30,94,20,detail_ink());
  lv_obj_t *button=detail_button(card,"返回桌面",GLASS_SIDEBAR_PAD,GLASS_CONTENT_HEIGHT-56,GLASS_SIDEBAR_ITEM_WIDTH,back,NULL);
  glass_sidebar_item(button,false);
}
