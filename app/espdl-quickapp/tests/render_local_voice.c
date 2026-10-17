/* SPDX-License-Identifier: Apache-2.0 */
#define GLASS_LOCAL_SPEECH 1
#define UNIFIED_DEVICE_NO_MAIN
#include "render_statusbar_devices.c"
extern void review_chat_dialog_fixture(bool),review_local_ha_fixture(bool),review_local_config_change(void);
extern void review_local_command_fixture(int,bool),review_local_ha_failure(bool,bool,bool),review_speech_done_fixture(int);
extern unsigned review_local_call_count(void);
static void local_steps(unsigned count)
{for(unsigned i=0;i<count;i++){lv_tick_inc(250);speech_ui_update(NULL);lv_timer_handler();}}
static void local_bind(unsigned role)
{
  lv_obj_send_event(lv_obj_get_parent(g_speech_ui.binding[role]),LV_EVENT_CLICKED,NULL);assert(g_speech_ui.picker);
  local_steps(2);assert(g_speech_ui.choice_count==1); /* garage covers excluded */
  lv_obj_send_event(g_speech_ui.rows[0],LV_EVENT_CLICKED,NULL);
  assert(!g_speech_ui.picker&&g_speech_ui.bound[role].entity[0]);
}
static void local_emit(int command)
{review_local_command_fixture(command,false);local_steps(10);}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];
  FILE *fontfile=fopen(argv[1],"rb");assert(fontfile);fseek(fontfile,0,SEEK_END);size_t size=ftell(fontfile);rewind(fontfile);
  void *font=malloc(size);assert(font&&fread(font,1,size,fontfile)==size);fclose(fontfile);
  lv_init();lv_display_t *display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(font,size,16);g_desktop.font20=lv_tiny_ttf_create_data(font,size,20);g_desktop.font28=lv_tiny_ttf_create_data(font,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(font,size,48);glass_home_fonts_init(font,size);
  g_glass.reduced_motion=true;g_glass.temperature_c=-999;g_glass.humidity_pct=-1;g_glass.pm25=-1;
  g_desktop.screen=glass_box(lv_screen_active(),0,GLASS_STATUS_HEIGHT,1024,GLASS_CONTENT_HEIGHT,0,0);
  lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,GLASS_CONTENT_HEIGHT,0,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  glass_edge_back_create();glass_status_create();
  assert(!local_voice_entity("light.bad.other",0)&&!local_voice_entity("light.x/../door",0));
  assert(!local_voice_entity("lock.door",0)&&!local_voice_entity("switch.heater",0));
  assert(!local_voice_json("[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]"));
  for(unsigned theme=0;theme<2;theme++){
    panel_hide(NULL);review_chat_dialog_fixture(true);review_local_ha_fixture(true);g_desktop.light_theme=theme==0;glass_rebuild();
    glass_speech_open(NULL);local_steps(2);assert(g_speech_ui.root);
    review_capture(display,"local-voice-home","端侧语音助手");
    for(unsigned role=0;role<3;role++)local_bind(role);
    assert(!review_local_call_count());review_capture(display,"local-voice-bound","本次会话设备绑定");
    review_click("开始离线聆听");assert(g_speech_ui.owner);
    local_emit(1);assert(review_local_call_count()==1&&g_speech_ui.stage==LOCAL_IDLE);
    assert(!strcmp(lv_label_get_text(g_speech_ui.feedback),"设备状态已确认"));
    for(unsigned i=0;i<20;i++)speech_ui_update(NULL);assert(review_local_call_count()==1);
    local_emit(1);assert(review_local_call_count()==1); /* already on, no POST */
    for(int command=2;command<=6;command++){local_emit(command);assert(review_local_call_count()==(unsigned)command);}
    review_capture(display,"local-voice-confirmed","离线指令执行后核对实际状态");
    review_local_command_fixture(1,true);local_steps(3);assert(review_local_call_count()==6);
    review_local_config_change();local_steps(2);assert(!g_speech_ui.bound[0].entity[0]);
    local_emit(1);assert(review_local_call_count()==6);
    review_click("停止");review_speech_done_fixture(0);local_steps(2);local_bind(0);
    review_click("开始离线聆听");
    review_local_ha_failure(false,true,false);local_emit(1);assert(review_local_call_count()==7);
    assert(!strcmp(lv_label_get_text(g_speech_ui.feedback),"设备状态已确认")); /* lost POST ack, successful GET */
    review_local_ha_failure(false,false,true);local_emit(2);assert(review_local_call_count()==7);
    review_local_ha_failure(true,false,false);local_emit(2);local_steps(48);assert(review_local_call_count()==8);
    assert(strstr(lv_label_get_text(g_speech_ui.feedback),"未确认"));
    review_capture(display,"local-voice-uncertain","状态未确认时不自动重发控制");
    review_local_ha_failure(false,false,false);
    review_local_command_fixture(2,false);speech_ui_update(NULL);assert(g_speech_ui.stage==LOCAL_PRECHECK);
    panel_hide(NULL);local_steps(3);assert(review_local_call_count()==8&&!g_speech_ui.client);
    review_speech_done_fixture(0);glass_speech_open(NULL);review_click("开始离线聆听");
    local_emit(7);assert(!g_speech_ui.root); /* return home is local */
    review_speech_done_fixture(0);glass_speech_open(NULL);review_click("开始离线聆听");
    local_emit(8);assert(g_speech_ui.root&&!strcmp(lv_label_get_text(g_speech_ui.feedback),"已停止聆听"));
    /* Retained events and simultaneous stop/home must win over HA polling. */
    review_speech_done_fixture(0);local_steps(1);local_bind(0);
    review_click("开始离线聆听");unsigned before=review_local_call_count();
    review_local_command_fixture(2,false);review_click("停止");local_steps(8);
    assert(review_local_call_count()==before&&g_speech_ui.stage==LOCAL_IDLE&&!g_speech_ui.accepting_commands);
    review_speech_done_fixture(0);review_click("开始离线聆听");
    review_local_command_fixture(2,false);speech_ui_update(NULL);assert(g_speech_ui.stage==LOCAL_PRECHECK);
    local_emit(8);assert(review_local_call_count()==before&&!g_speech_ui.client);
    assert(strstr(lv_label_get_text(g_speech_ui.feedback),"已取消"));
    review_speech_done_fixture(0);review_click("开始离线聆听");
    review_local_command_fixture(2,false);speech_ui_update(NULL);assert(g_speech_ui.stage==LOCAL_PRECHECK);
    local_emit(7);assert(review_local_call_count()==before&&!g_speech_ui.root&&!g_speech_ui.client);
    review_speech_done_fixture(0);glass_speech_open(NULL);local_bind(0);review_click("开始离线聆听");
    review_local_command_fixture(2,false);speech_ui_update(NULL);speech_ui_update(NULL);
    assert(g_speech_ui.stage==LOCAL_CONTROL&&review_local_call_count()==before+1);
    review_click("停止");local_steps(8);
    assert(!g_speech_ui.client&&review_local_call_count()==before+1);
    assert(strstr(lv_label_get_text(g_speech_ui.feedback),"已发送"));
    review_local_command_fixture(1,false);local_steps(8);assert(review_local_call_count()==before+1);
    review_speech_done_fixture(0);review_click("开始离线聆听");local_emit(1);
    assert(review_local_call_count()==before+2&&g_speech_ui.stage==LOCAL_IDLE);
    review_capture(display,"local-voice-stop-safe","停止优先于待处理控制，重新启动后恢复接收指令");
    panel_hide(NULL);review_speech_done_fixture(0);review_local_ha_fixture(false);
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("LOCAL_VOICE_UI_OK: %u captures; both themes; 3 explicit bindings, 6 actions, state verification, deduplication, stale commands, configuration changes, unavailable devices, uncertain results, cancellation, home, stop/precheck races, retained-event disarming and explicit restart; hardware/network mocked\n",review_count);
  return 0;
}
