/* SPDX-License-Identifier: Apache-2.0 */
#define main unused_all_pages_main
#include "render_all_ui.c"
#undef main
extern void review_grok_fixture(int mode);
static void mosaic_frames(unsigned count){for(unsigned i=0;i<count;i++){lv_tick_inc(50);glass_grok_test_step_visible();usleep(3000);}}
static void settled_home(void){glass_rebuild();for(unsigned i=0;i<GLASS_HOME_CARD_COUNT;i++){lv_anim_delete(g_glass.home_cards[i],glass_card_offset);lv_obj_set_style_translate_y(g_glass.home_cards[i],0,0);}lv_obj_update_layout(g_desktop.screen);mosaic_frames(30);lv_obj_update_layout(g_desktop.screen);}
int main(int argc,char **argv){
  assert(argc==3);review_out=argv[2];FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);void *data=malloc(size);assert(data&&fread(data,1,size,f)==size);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(data,size,16);g_desktop.font20=lv_tiny_ttf_create_data(data,size,20);g_desktop.font28=lv_tiny_ttf_create_data(data,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(data,size,48);glass_home_fonts_init(data,size);g_glass.reduced_motion=false;
  g_desktop.screen=lv_screen_active();lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,600,0,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  const char *keys[]={"idle","thinking","replying","busy","done","error","stopped","listening"};
  const char *names[]={"待机","思考","输出回复","处理草稿","完成","出错","停止","聆听"};
  for(unsigned theme=0;theme<2;theme++){
    g_desktop.light_theme=theme==0;review_grok_fixture(0);settled_home();
    lv_obj_t *card=g_glass.home_cards[1];lv_area_t bounds,view;lv_obj_get_coords(card,&bounds);lv_obj_get_coords(lv_obj_get_child(card,0),&view);
    assert(bounds.x1==108&&bounds.y1==374&&lv_area_get_width(&bounds)==285&&lv_area_get_height(&bounds)==178);
    assert(view.x1==108&&view.y1==374&&lv_area_get_width(&view)==285&&lv_area_get_height(&view)==89);
    for(unsigned alternative=0;alternative<2;alternative++){
      bool black=(theme==1)^alternative;
      if(alternative){
        review_grok_fixture(0);lv_obj_delete(lv_obj_get_child(card,0));
        struct glass_grok *ball=glass_grok_create_pixel(card,285,black,zh_font);assert(ball);
        lv_obj_update_layout(g_desktop.screen);mosaic_frames(30);assert(glass_grok_ready(ball));
      }
      for(unsigned state=0;state<8;state++){
      review_grok_fixture(state);mosaic_frames(state==4?6:30);
      char id[64],title[128];snprintf(id,sizeof(id),"pixel-%s-%s",black?"black":"white",keys[state]);snprintf(title,sizeof(title),"原版像素球 · %s",names[state]);review_capture(display,id,title);
      if(state==0||state==1||state==2){for(unsigned frame=0;frame<30;frame++){mosaic_frames(2);lv_refr_now(display);snprintf(path,sizeof(path),"%s/%s-%s-%s-frame-%03u.rgb565",review_out,theme?"dark":"light",black?"black":"white",keys[state],frame);FILE *shot=fopen(path,"wb");assert(shot);fwrite(review_fb,1,sizeof(review_fb),shot);fclose(shot);}}
      }
    }
    lv_obj_send_event(review_find(card,"开始对话"),LV_EVENT_CLICKED,NULL);assert(g_chat_ui.root);panel_hide(NULL);
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);lv_obj_delete(g_desktop.screen);
  lv_tiny_ttf_destroy(g_desktop.font16);lv_tiny_ttf_destroy(g_desktop.font20);lv_tiny_ttf_destroy(g_desktop.font28);lv_tiny_ttf_destroy(g_glass.font48);
  lv_tiny_ttf_destroy(g_home_font12);lv_tiny_ttf_destroy(g_home_font14);lv_tiny_ttf_destroy(g_home_font32);lv_tiny_ttf_destroy(g_home_font34);lv_tiny_ttf_destroy(g_home_font40);lv_tiny_ttf_destroy(g_home_font64);
  lv_display_delete(display);lv_deinit();free(data);puts("HOME_PIXEL_NATIVE_OK: both pixel palettes in both themes; same A card/viewport; eight states each; continuous black/white frames; chat callback retained; cleaned up");return 0;
}
