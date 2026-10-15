/* SPDX-License-Identifier: Apache-2.0 */
#define main unused_all_pages_main
#include "render_all_ui.c"
#undef main
extern void review_grok_fixture(int mode);
static void grok_frames(unsigned frames){for(unsigned i=0;i<frames;i++){lv_tick_inc(50);glass_grok_test_step_visible();}}
int main(int argc,char **argv){
  assert(argc==3);review_out=argv[2];FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);void *data=malloc(size);assert(data&&fread(data,1,size,f)==size);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(data,size,16);g_desktop.font20=lv_tiny_ttf_create_data(data,size,20);g_desktop.font28=lv_tiny_ttf_create_data(data,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(data,size,48);glass_home_fonts_init(data,size);g_glass.reduced_motion=false;
  g_desktop.screen=lv_screen_active();lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,600,0,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  snprintf(path,sizeof(path),"%s/avatar-positions.json",review_out);FILE *positions=fopen(path,"w");assert(positions);fputs("[\n",positions);unsigned position_count=0;
  const char *keys[]={"idle","thinking","replying","busy","done","error","stopped","listening"};
  const char *names[]={"待机","思考","输出回复","处理草稿","完成","出错","停止","语音聆听"};
  const unsigned expected[]={2,30,39,32,33,34,41,35};
  for(unsigned theme=0;theme<2;theme++){
    panel_hide(NULL);review_grok_fixture(0);g_desktop.light_theme=theme==0;glass_rebuild();grok_frames(30);review_capture(display,"home-grok","首页 · A 版小球");
    glass_chat_show(false);grok_frames(30);assert(glass_grok_ready(g_chat_ui.ball));review_capture(display,"chat-grok-idle","ESPClaw · 待机");
    for(int mode=1;mode<8;mode++){
      review_grok_fixture(mode);chat_ui_refresh(NULL);grok_frames(mode==4?6:30);assert(glass_grok_current(g_chat_ui.ball)==expected[mode]);
      chat_ui_refresh(NULL);
      lv_area_t anchor,mount,strip;lv_obj_get_coords(g_chat_ui.ball_anchor,&anchor);lv_obj_get_coords(g_chat_ui.ball_mount,&mount);lv_obj_get_coords(g_chat_ui.ball_strip,&strip);
      assert(mount.y1==anchor.y1&&mount.x1==250);if(mode!=7)assert(mount.x2<anchor.x1);
      struct glass_grok *same=g_chat_ui.ball;int before_y=mount.y1;int before_scroll=lv_obj_get_scroll_y(g_chat_ui.messages);
      lv_obj_scroll_to_y(g_chat_ui.messages,before_scroll+24,LV_ANIM_OFF);chat_ui_ball_position(NULL);lv_obj_get_coords(g_chat_ui.ball_mount,&mount);lv_obj_get_coords(g_chat_ui.ball_anchor,&anchor);assert(mount.y1==anchor.y1&&g_chat_ui.ball==same);
      lv_obj_scroll_to_y(g_chat_ui.messages,before_scroll,LV_ANIM_OFF);chat_ui_ball_position(NULL);lv_obj_get_coords(g_chat_ui.ball_mount,&mount);assert(mount.y1==before_y);
      char id[64],name[128];snprintf(id,sizeof(id),"chat-grok-%s",keys[mode]);snprintf(name,sizeof(name),"ESPClaw · %s",names[mode]);review_capture(display,id,name);
      fprintf(positions,"%s{\"theme\":\"%s\",\"state\":\"%s\",\"box\":[%d,%d,64,64]}",position_count++?",\n":"",theme?"dark":"light",keys[mode],mount.x1,mount.y1);
      if(mode==1||mode==2||mode==7){for(unsigned frame=0;frame<30;frame++){grok_frames(2);lv_refr_now(display);snprintf(path,sizeof(path),"%s/%s-%s-frame-%03u.rgb565",review_out,theme?"dark":"light",keys[mode],frame);FILE *shot=fopen(path,"wb");assert(shot);fwrite(review_fb,1,sizeof(review_fb),shot);fclose(shot);}}
    }
    review_grok_fixture(0);chat_ui_refresh(NULL);review_click("聊点什么…");assert(g_chat_ui.editor&&lv_obj_has_flag(g_chat_ui.ball_mount,LV_OBJ_FLAG_HIDDEN));grok_frames(6);review_capture(display,"chat-grok-input","ESPClaw · 输入");review_click("收起");assert(!g_chat_ui.editor&&!lv_obj_has_flag(g_chat_ui.ball_mount,LV_OBJ_FLAG_HIDDEN));
    review_click("返回桌面");assert(!g_chat_ui.root&&lv_obj_has_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN));grok_frames(6);
  }
  fputs("\n]\n",positions);fclose(positions);fputs("\n]\n",review_manifest);fclose(review_manifest);qpk_runtime_stop();lv_obj_delete(g_desktop.screen);
  lv_tiny_ttf_destroy(g_desktop.font16);lv_tiny_ttf_destroy(g_desktop.font20);lv_tiny_ttf_destroy(g_desktop.font28);lv_tiny_ttf_destroy(g_glass.font48);
  lv_tiny_ttf_destroy(g_home_font12);lv_tiny_ttf_destroy(g_home_font14);lv_tiny_ttf_destroy(g_home_font32);lv_tiny_ttf_destroy(g_home_font34);lv_tiny_ttf_destroy(g_home_font40);lv_tiny_ttf_destroy(g_home_font64);
  lv_display_delete(display);lv_deinit();free(data);puts("GROK_ESPCLAW_PAGES_OK: production home/chat; two themes; 8 actual mapped states; input/back; continuous frames");return 0;
}
