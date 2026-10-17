/* SPDX-License-Identifier: Apache-2.0 */
#define main unused_all_pages_main
#include "render_all_ui.c"
#undef main
extern double qpk_review_number(const char *source);
static lv_indev_state_t motion_pointer_state;
static lv_point_t motion_pointer_point;
static void motion_pointer_read(lv_indev_t *input,lv_indev_data_t *data)
{LV_UNUSED(input);data->state=motion_pointer_state;data->point=motion_pointer_point;}
static void motion_pointer(lv_indev_t *input,int x,int y,bool pressed)
{motion_pointer_point=(lv_point_t){x,y};motion_pointer_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;lv_obj_update_layout(lv_layer_top());lv_indev_read(input);}
static void fluid_frames(lv_display_t *display,const char *name,unsigned count)
{
  char path[1024];
  for(unsigned i=0;i<count;i++){
    for(unsigned ms=0;ms<GLASS_MOTION_TICK;ms++){lv_tick_inc(1);lv_timer_handler();}
    lv_refr_now(display);
    snprintf(path,sizeof(path),"%s/%s-%s-%03u.rgb565",review_out,g_desktop.light_theme?"light":"dark",name,i);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(review_fb,1,sizeof(review_fb),f)==sizeof(review_fb));fclose(f);
  }
}
static void fluid_page(unsigned page)
{
  char js[256];snprintf(js,sizeof(js),"globalThis.__fluidDot=ESPHomeHA.snapshot().pageIndicator.slots.find(s=>s.page===%u)",page);
  assert(qpk_review_eval(js)==0);
  assert(qpk_review_click_at((int)qpk_review_number("__fluidDot.x+12"),(int)qpk_review_number("__fluidDot.y+20"))==0);
  assert(qpk_review_eval("for(let i=0;i<12;i++)__reviewTick()")==0);
}
static void motion_frames(lv_display_t *display,const char *name,unsigned count)
{
  char path[1024];
  for(unsigned i=0;i<count;i++){
    lv_tick_inc(20);lv_timer_handler();lv_refr_now(display);
    snprintf(path,sizeof(path),"%s/%s-%s-%03u.rgb565",review_out,
             g_desktop.light_theme?"light":"dark",name,i);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(review_fb,1,sizeof(review_fb),f)==sizeof(review_fb));fclose(f);
  }
}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];FILE *f=fopen(argv[1],"rb");assert(f);
  fseek(f,0,SEEK_END);size_t bytes=ftell(f);rewind(f);void *data=malloc(bytes);
  assert(data&&fread(data,1,bytes,f)==bytes);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(data,bytes,16);
  g_desktop.font20=lv_tiny_ttf_create_data(data,bytes,20);
  g_desktop.font28=lv_tiny_ttf_create_data(data,bytes,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);
  g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(data,bytes,48);glass_home_fonts_init(data,bytes);
  g_desktop.screen=lv_screen_active();lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,600,0,0);
  lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);
  review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  for(unsigned theme=0;theme<2;theme++){
    g_desktop.light_theme=theme==0;g_glass.reduced_motion=false;glass_motion_set_reduced(false);
    glass_rebuild();glass_apps(NULL);lv_obj_update_layout(g_desktop.screen);
    lv_obj_t *apps_card=g_apps_card;
    /* Real LVGL pointer dispatch: a short drag restores immediately; a committed drag closes on dispatch. */
    if(theme==0)glass_edge_back_create();
    lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,motion_pointer_read);
    motion_pointer(pointer,5,300,true);motion_pointer(pointer,45,300,true);
    assert(g_edge_tracking&&lv_obj_get_style_translate_x(apps_card,0)>0);
    fluid_frames(display,"edge-follow",2);motion_pointer(pointer,45,300,false);
    assert(lv_obj_get_style_translate_x(apps_card,0)==0);fluid_frames(display,"edge-spring-back",1);
    motion_pointer(pointer,5,300,true);motion_pointer(pointer,100,300,true);
    motion_pointer(pointer,100,300,false);fluid_frames(display,"edge-commit",1);
    assert(!g_desktop.current_card&&!g_apps_card&&!g_apps_pager);
    glass_apps(NULL);fluid_frames(display,"apps-reopen",12);
    apps_card=g_apps_card;
    motion_pointer(pointer,1019,300,true);motion_pointer(pointer,979,300,true);
    assert(g_edge_tracking&&lv_obj_get_style_translate_x(apps_card,0)<0);
    motion_pointer(pointer,979,300,false);fluid_frames(display,"edge-right-cancel",12);
    assert(lv_obj_get_style_translate_x(apps_card,0)==0);
    motion_pointer(pointer,5,300,true);motion_pointer(pointer,45,300,true);
    motion_pointer(pointer,45,360,true);assert(!g_edge_tracking);
    motion_pointer(pointer,45,360,false);fluid_frames(display,"edge-vertical-cancel",12);
    assert(lv_obj_get_style_translate_x(apps_card,0)==0);
    glass_motion_set_reduced(true);
    motion_pointer(pointer,5,300,true);motion_pointer(pointer,45,300,true);
    assert(lv_obj_get_style_translate_x(apps_card,0)==0);
    motion_pointer(pointer,45,300,false);glass_motion_set_reduced(false);
    motion_pointer(pointer,5,300,true);motion_pointer(pointer,45,300,true);
    panel_hide(NULL);motion_pointer(pointer,45,300,false);assert(!g_edge_page);
    lv_indev_delete(pointer);panel_hide(NULL);
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  puts("FAST_EXIT_UI_OK: both themes; left/right cancellation, next-dispatch return, vertical cancel, reduced motion, deletion during drag");
  return 0;
}
