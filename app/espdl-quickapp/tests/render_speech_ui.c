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
    assert(review_label_find(g_desktop.screen,"本地语音"));review_click("本地语音");
    assert(g_speech_ui.timer);review_capture(display,"speech-idle","中文离线语音入口");
    review_click("开始离线聆听");speech_ui_update(NULL);assert(preview_listens==theme+1);
    assert(review_label_find(g_desktop.screen,"等待唤醒"));review_capture(display,"speech-wake","等待离线唤醒");
    review_click("停止");speech_ui_update(NULL);assert(!preview_speech.active);
    review_click("测试中文播报");speech_ui_update(NULL);assert(preview_says==theme+1);
    assert(review_label_find(g_desktop.screen,"正在中文播报"));review_capture(display,"speech-speaking","中文播报状态");
    preview_speech.error=-ENOENT;preview_speech.revision++;speech_ui_update(NULL);
    assert(review_label_find(g_desktop.screen,"请安装匹配的 SD 语音资源"));review_capture(display,"speech-missing-model","语音模型缺失提示");
    panel_hide(NULL);assert(!g_speech_ui.timer&&!preview_speech.active);
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("SPEECH_UI_NATIVE_OK: %u captures, manual start/stop/say callbacks and page deletion; mocked speech service\n",review_count);
  return 0;
}
