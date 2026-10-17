/* SPDX-License-Identifier: Apache-2.0 */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_unified_devices.c"
extern void review_music_now_fixture(const struct music_now_playing *);
extern void review_lock_photo_fixture(int);
extern void review_cover_fixture(const char *);
static lv_point_t touch_point;
static lv_indev_state_t touch_state;
static unsigned leaked_touches;
static void touch_read(lv_indev_t *input,lv_indev_data_t *data)
{(void)input;data->point=touch_point;data->state=touch_state;}
static void touch(lv_indev_t *input,int x,int y,bool pressed)
{touch_point=(lv_point_t){x,y};touch_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(input);}
static void underlying_click(lv_event_t *e){(void)e;leaked_touches++;}
static unsigned lock_object_count(lv_obj_t *root)
{unsigned count=1;for(unsigned i=0;i<lv_obj_get_child_count(root);i++)count+=lock_object_count(lv_obj_get_child(root,i));return count;}
static void fixed_assert(void)
{
  lv_obj_t *shells[]={lv_screen_active(),lv_layer_top(),g_desktop.screen,g_desktop.panel};
  for(unsigned i=0;i<4;i++){assert(!lv_obj_has_flag(shells[i],LV_OBJ_FLAG_SCROLLABLE));assert(!lv_obj_get_scroll_x(shells[i])&&!lv_obj_get_scroll_y(shells[i]));}
}
static void fixed_drag(lv_indev_t *input,int x,int from,int to)
{
  touch(input,x,from,true);
  for(int i=1;i<=12;i++){touch(input,x,from+(to-from)*i/12,true);fixed_assert();}
  touch(input,x,to,false);for(unsigned i=0;i<20;i++){lv_tick_inc(16);lv_timer_handler();fixed_assert();}
}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];
  FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
  void *font=malloc(size);assert(font);assert(fread(font,1,size,f)==size);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(font,size,16);
  g_desktop.font20=lv_tiny_ttf_create_data(font,size,20);
  g_desktop.font28=lv_tiny_ttf_create_data(font,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(font,size,48);glass_home_fonts_init(font,size);
  g_glass.reduced_motion=true;g_glass.temperature_c=-999;g_glass.humidity_pct=-1;g_glass.pm25=-1;
  g_desktop.screen=glass_box(lv_screen_active(),0,GLASS_STATUS_HEIGHT,1024,GLASS_CONTENT_HEIGHT,0,0);lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,GLASS_CONTENT_HEIGHT,0x000000,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);




  g_glass.font72=lv_tiny_ttf_create_data(font,size,72);
  lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,touch_read);lv_indev_set_display(input,display);

  glass_edge_back_create();glass_status_create();
  for(unsigned theme=0;theme<2;theme++){
    g_desktop.light_theme=theme==0;glass_rebuild();fixed_assert();
    fixed_drag(input,660,90,360);review_capture(display,"fixed-home","桌面 · 外壳不可下拉");
    glass_settings(NULL);fixed_drag(input,980,90,360);fixed_drag(input,980,360,90);
    review_capture(display,"fixed-settings","设置 · 外壳不可拖动");
    glass_chat_show(false);fixed_drag(input,1000,100,390);panel_hide(NULL);fixed_assert();
    /* A tall inner list still handles normal touch scrolling. */
    lv_obj_t *card=panel_card("scroll fixture");lv_obj_clean(card);
    lv_obj_set_pos(card,0,0);lv_obj_set_size(card,1024,GLASS_CONTENT_HEIGHT);
    lv_obj_t *list=glass_box(card,200,70,600,360,detail_paper(),0);
    lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(list,LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_scroll_dir(list,LV_DIR_VER);
    lv_obj_t *extent=glass_box(list,0,0,580,1200,detail_soft(),0);
    lv_obj_remove_flag(extent,LV_OBJ_FLAG_SCROLLABLE);lv_obj_update_layout(card);
    fixed_drag(input,500,400,170);assert(lv_obj_get_scroll_y(list)>0);
    lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);fixed_drag(input,500,180,400);assert(lv_obj_get_scroll_y(list)==0);
    panel_hide(NULL);fixed_assert();
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  puts("FIXED_SHELL_OK: both themes; actual pointer drags on home/settings/chat; shell offsets remain zero during drag, release and inertia; inner list scrolls; top boundary stays fixed");
  return 0;
}
