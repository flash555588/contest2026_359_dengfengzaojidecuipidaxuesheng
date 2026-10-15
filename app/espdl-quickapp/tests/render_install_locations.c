/* SPDX-License-Identifier: Apache-2.0 */
#define GLASS_LOCAL_SPEECH 1
#include "glass_speech.h"
static struct speech_snapshot preview_speech;
static unsigned preview_listens,preview_says;
void glass_speech_get(struct speech_snapshot *out){*out=preview_speech;}
int glass_speech_listen(void){preview_listens++;preview_speech.active=true;preview_speech.phase=SPEECH_WAKE;preview_speech.error=0;preview_speech.revision++;return 0;}
int glass_speech_say(const char *text){(void)text;preview_says++;preview_speech.active=true;preview_speech.phase=SPEECH_SPEAKING;preview_speech.error=0;preview_speech.revision++;return 0;}
void glass_speech_stop(void){preview_speech.active=false;preview_speech.phase=SPEECH_IDLE;preview_speech.error=0;preview_speech.revision++;}
/* Scene and session-log navigation, with production QuickJS/LVGL and offline I/O. */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_unified_devices.c"

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
  g_desktop.screen=lv_screen_active();lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,600,0x000000,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);



  for(unsigned theme=0;theme<2;theme++){
    panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();glass_chat_show(false);
    chat_ui_save_app(NULL);assert(g_chat_ui.confirm);lv_obj_update_layout(g_desktop.screen);
    assert(review_label_find(g_desktop.screen,"选择安装位置"));
    assert(review_label_find(g_desktop.screen,"内置存储"));assert(review_label_find(g_desktop.screen,"SD 卡未就绪"));
    review_capture(display,"install-location","快应用安装位置");
    review_click("取消");assert(!g_chat_ui.confirm);
    g_desktop.qpk=realloc(g_desktop.qpk,2*sizeof(*g_desktop.qpk));assert(g_desktop.qpk);
    memset(g_desktop.qpk,0,2*sizeof(*g_desktop.qpk));g_desktop.qpk_capacity=g_desktop.nqpk=2;
    strcpy(g_desktop.qpk[0].name,"内置测试应用");strcpy(g_desktop.qpk[0].package,"test.internal");strcpy(g_desktop.qpk[0].dir,"test.internal");
    strcpy(g_desktop.qpk[1].name,"SD 测试应用");strcpy(g_desktop.qpk[1].package,"test.sd");strcpy(g_desktop.qpk[1].dir,"test.sd");g_desktop.qpk[1].volume=QPK_VOLUME_SD;
    g_apps_page_index=1;glass_apps_render();lv_obj_update_layout(g_desktop.screen);
    assert(review_label_find(g_desktop.screen,"SD 卡应用"));review_capture(display,"app-storage","应用安装来源");
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("INSTALL_UI_NATIVE_OK: %u captures, location chooser/cancel and per-volume app labels; mocked services\n",review_count);
  return 0;
}
