/* SPDX-License-Identifier: Apache-2.0 */
#define main all_ui_reference_main
#include "render_all_ui.c"
#undef main

static void check_full_page(void)
{
  lv_obj_t *card=g_desktop.current_card;
  assert(card);
  lv_obj_update_layout(g_desktop.screen);
  assert(lv_obj_get_x(card)==0 && lv_obj_get_y(card)==0);
  assert(lv_obj_get_width(card)==1024);
  assert(lv_obj_get_height(card)==GLASS_CONTENT_HEIGHT);
  assert(lv_obj_get_style_bg_opa(g_desktop.panel,0)==LV_OPA_TRANSP);
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
  g_desktop.screen=glass_box(lv_screen_active(),0,GLASS_STATUS_HEIGHT,1024,GLASS_CONTENT_HEIGHT,0,0);
  lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,GLASS_CONTENT_HEIGHT,0,0);
  lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);
  review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  glass_status_create();
  lv_obj_update_layout(lv_layer_top());

  for(unsigned theme=0;theme<2;theme++){
    g_desktop.light_theme=theme==0;g_glass.reduced_motion=true;glass_rebuild();
    for(unsigned page=0;page<6;page++){
      panel_hide(NULL);
      if(page==1)glass_settings(NULL);
      if(page==2)glass_storage_page(NULL);
      if(page==3)glass_chat_open(NULL);
      if(page==4)glass_music(NULL);
      if(page==5)glass_lock_open(NULL);
      assert(!g_wake_mini.visible);
      wake_mini_present(theme*6+page+1,lv_tick_get());
      lv_obj_update_layout(lv_layer_top());
      for(int i=0;i<50&&!glass_grok_ready(g_wake_mini.ball);i++){usleep(10000);glass_grok_test_step(g_wake_mini.ball);}
      assert(glass_grok_ready(g_wake_mini.ball));
      lv_obj_update_layout(lv_layer_top());lv_area_t area;lv_obj_get_coords(g_wake_mini.root,&area);
      assert(area.x1==16&&area.y1==472);
      static const char *names[]={"home","settings","files","chat","music","lock"};
      review_capture(display,names[page],names[page]);
      lv_area_t body;lv_obj_get_coords(g_wake_mini.text,&body);
      printf("MINI_TEXT page=%u visible=%u area=%ld,%ld,%ld,%ld opacity=%u text=%s\n",page,
        lv_obj_is_visible(g_wake_mini.text),(long)body.x1,(long)body.y1,(long)body.x2,(long)body.y2,
        lv_obj_get_style_text_opa(g_wake_mini.text,0),lv_label_get_text(g_wake_mini.text));
      wake_mini_hide();if(page==5)lock_unlock(NULL);
    }
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  lv_obj_delete(g_wake_mini.root);
  puts("WAKE_MINI_PAGES_OK home/settings/files/chat/music/lock; both themes; actual production layouts and mini assets");
}
