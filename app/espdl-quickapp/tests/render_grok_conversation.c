#define GLASS_LOCAL_SPEECH 1
/* SPDX-License-Identifier: Apache-2.0 */
/* Both production entry points, one shared conversation fixture; no real I/O. */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_statusbar_devices.c"
#include "glass_speech_level.h"
extern void review_chat_dialog_fixture(bool);
extern void review_chat_reply_fixture(const char *,bool);
extern void review_voice_text_fixture(const char *);
extern unsigned review_chat_send_count(void),review_chat_cancel_count(void),review_voice_start_count(void);
static void frames(unsigned n)
{for(unsigned i=0;i<n;i++){lv_tick_inc(100);lv_timer_handler();glass_grok_test_step_visible();usleep(2000);}}
static unsigned objects(lv_obj_t *root)
{unsigned n=1;for(unsigned i=0;i<lv_obj_get_child_count(root);i++)n+=objects(lv_obj_get_child(root,i));return n;}
static void send_text(const char *text)
{
  chat_ui_editor_open(NULL);assert(g_chat_ui.input);
  lv_textarea_set_text(g_chat_ui.input,text);chat_ui_send(NULL);assert(!g_chat_ui.editor);frames(5);
}
extern void review_speech_done_fixture(int),review_speech_failure_fixture(int);
extern unsigned review_speech_count(void);
extern const char *review_speech_text(void);
extern void review_voice_level_fixture(unsigned),review_voice_error_fixture(void);
extern void review_speech_level_fixture(unsigned),review_chat_activity_fixture(enum chat_activity);
extern void review_chat_phase_fixture(enum chat_phase),review_chat_error_fixture(void);
extern void review_face_fixture(int,int,bool),review_face_available_fixture(bool);
extern unsigned review_face_cancel_count(void);
static void tap(void){lv_obj_send_event(glass_grok_root(g_chat_ui.ball),LV_EVENT_CLICKED,NULL);frames(4);}
static void run_voice_cases(lv_display_t *display)
{
  for(unsigned theme=0;theme<2;theme++){
    chat_interface_modes[0]=AGENT_MODE_CREATE;chat_interface_modes[1]=AGENT_MODE_CHAT;
    review_chat_dialog_fixture(true);g_desktop.light_theme=theme==0;glass_rebuild();
    review_face_available_fixture(false);
    launch_builtin_ouo_qapp(NULL);assert(g_chat_ui.ball_mode&&g_chat_ui.ball);frames(16);
    g_glass.reduced_motion=false;glass_grok_set_reduced_motion(g_chat_ui.ball,false);
    assert(!g_chat_ui.messages&&!g_chat_ui.editor&&!g_chat_ui.keyboard);
    assert(review_voice_start_count()==0&&review_speech_count()==0);
    assert(g_chat_ui.voice_wave==BALL_WAVE_OFF&&lv_timer_get_paused(g_chat_ui.voice_wave_timer));
    for(unsigned i=0;i<25;i++)assert(g_chat_ui.voice_wave_heights[i]==0);
    review_capture(display,"voice-idle","语音小球 · 轻点开始");
    assert(!g_chat_ui.voice_face_request);
    lv_obj_update_layout(g_chat_ui.root);assert(lv_obj_get_width(g_chat_ui.voice_waveform)==120);
    review_face_available_fixture(true);chat_ball_face_toggle(NULL);assert(g_chat_ui.voice_face_request);
    review_face_fixture(-1000,-1000,true);frames(5);assert(g_chat_ui.voice_gaze_x<0&&g_chat_ui.voice_gaze_y<0);
    review_capture(display,"face-left","语音小球 · 看向左侧人脸");
    review_face_fixture(1000,1000,true);frames(7);assert(g_chat_ui.voice_gaze_x>0&&g_chat_ui.voice_gaze_y>0);
    review_capture(display,"face-right","语音小球 · 看向右侧人脸");
    /* A stalled frame must not hold the gaze indefinitely. */
    frames(26);assert(abs(g_chat_ui.voice_gaze_x)<125&&abs(g_chat_ui.voice_gaze_y)<166);
    review_face_fixture(600,200,true);frames(5);review_face_fixture(0,0,false);frames(16);
    assert(abs(g_chat_ui.voice_gaze_x)<125);
    unsigned cancels=review_face_cancel_count();chat_ball_face_toggle(NULL);
    assert(!g_chat_ui.voice_face_request&&review_face_cancel_count()==cancels+1);
    review_face_available_fixture(false);frames(8);
    unsigned warm=objects(g_chat_ui.root);struct glass_grok *ball=g_chat_ui.ball;
    for(unsigned i=0;i<100;i++)chat_ui_refresh(NULL);
    assert(objects(g_chat_ui.root)==warm&&g_chat_ui.ball==ball);
    tap();assert(g_chat_ui.voice_session&&review_voice_start_count()==1);
    chat_ui_modes(NULL);assert(!g_chat_ui.mode_panel&&g_chat_ui.mode==AGENT_MODE_CHAT);
    assert(g_chat_ui.voice_wave==BALL_WAVE_LISTEN&&!lv_timer_get_paused(g_chat_ui.voice_wave_timer));
    assert(glass_grok_current(ball)==35);review_capture(display,"voice-listening","语音小球 · 我在听");
    unsigned live_objects=objects(g_chat_ui.root);review_voice_level_fixture(6000);
    for(unsigned i=0;i<100;i++){lv_tick_inc(20);chat_ball_wave_tick(NULL);}
    unsigned loud=g_chat_ui.voice_wave_level;assert(loud>240);
    for(unsigned i=0;i<25;i++)assert(g_chat_ui.voice_wave_heights[i]>=4&&g_chat_ui.voice_wave_heights[i]<=44);
    review_voice_level_fixture(0);for(unsigned i=0;i<20;i++)chat_ball_wave_tick(NULL);
    assert(g_chat_ui.voice_wave_level<loud&&objects(g_chat_ui.root)==live_objects);
    g_glass.reduced_motion=true;chat_ball_hint("我在听",35);
    uint8_t reduced_bars[25];memcpy(reduced_bars,g_chat_ui.voice_wave_heights,25);
    chat_ball_wave_tick(NULL);assert(!memcmp(reduced_bars,g_chat_ui.voice_wave_heights,25));
    assert(lv_timer_get_paused(g_chat_ui.voice_wave_timer));
    g_glass.reduced_motion=false;chat_ball_hint("我在听",35);assert(!lv_timer_get_paused(g_chat_ui.voice_wave_timer));
    review_voice_level_fixture(2200);frames(4);review_voice_level_fixture(0);frames(18);
    assert(!review_chat_send_count()); /* Final ASR result has not arrived. */
    review_voice_text_fixture("你好，小球");frames(4);
    assert(review_chat_send_count()==1&&g_chat_ui.voice_phase==BALL_REPLY);
    assert(g_chat_ui.snapshot->count==1&&!strcmp(g_chat_ui.snapshot->turns[0].prompt,"你好，小球"));
    chat_ui_modes(NULL);assert(!g_chat_ui.mode_panel&&g_chat_ui.mode==AGENT_MODE_CHAT);
    review_chat_phase_fixture(CHAT_QUEUED);frames(4);assert(glass_grok_current(ball)==31);
    review_chat_phase_fixture(CHAT_REQUESTING);review_chat_activity_fixture(CHAT_ACTIVITY_CONNECT);frames(4);assert(glass_grok_current(ball)==36);
    review_chat_activity_fixture(CHAT_ACTIVITY_THINK);frames(4);assert(glass_grok_current(ball)==30);
    review_capture(display,"voice-thinking","语音小球 · 思考");
    review_chat_activity_fixture(CHAT_ACTIVITY_TOOL);frames(4);assert(glass_grok_current(ball)==32);
    review_capture(display,"voice-tool","语音小球 · AI 工具事件驱动");
    review_chat_reply_fixture("你好，我在。",false);frames(6);assert(!review_speech_count());
    assert(glass_grok_current(ball)==39&&g_chat_ui.voice_wave==BALL_WAVE_THINK);
    review_capture(display,"voice-stream","语音小球 · AI 流式回复驱动");
    review_chat_reply_fixture("你好，我在。我们可以接着聊。",true);frames(4);
    assert(review_speech_count()==1&&g_chat_ui.voice_phase==BALL_SPEAK);
    assert(glass_grok_current(ball)==39&&g_chat_ui.voice_wave==BALL_WAVE_SPEAK);
    review_speech_level_fixture(24000);for(unsigned i=0;i<12;i++)chat_ball_wave_tick(NULL);
    assert(g_chat_ui.voice_wave_level>250);
    assert(!strcmp(review_speech_text(),"你好，我在。我们可以接着聊。"));
    review_capture(display,"voice-speaking","语音小球 · 语音回答");
    for(unsigned i=0;i<13;i++){
      review_speech_level_fixture(8000+(i*7919)%20000);
      char name[40];snprintf(name,sizeof(name),"voice-wave-%02u",i);
      frames(1);review_capture(display,name,"语音小球 · 原生波纹帧");
    }
    frames(20);assert(review_voice_start_count()==1); /* Speaker still owns audio. */
    review_speech_done_fixture(0);frames(12);assert(review_voice_start_count()==2);
    assert(g_chat_ui.voice_phase==BALL_LISTEN);
    review_voice_text_fixture("继续刚才的话题");frames(4);assert(review_chat_send_count()==2);
    tap();assert(review_chat_cancel_count()==1&&glass_grok_current(ball)==41&&g_chat_ui.voice_wave==BALL_WAVE_OFF);frames(10);
    assert(review_voice_start_count()==3); /* Interrupt reply then listen again. */
    review_voice_text_fixture("再说一句");frames(4);review_chat_reply_fixture("当然，我一直在这里。",true);frames(4);
    unsigned calls=review_speech_count();tap();frames(12);
    assert(review_voice_start_count()==3); /* Stop request is not completion. */
    review_speech_done_fixture(0);frames(10);assert(review_voice_start_count()==4);
    chat_ball_end(NULL);review_voice_text_fixture("迟到的识别结果");frames(12);
    assert(review_chat_send_count()==3&&!g_chat_ui.voice_session&&review_speech_count()==calls);
    assert(glass_grok_current(ball)==2&&g_chat_ui.voice_wave==BALL_WAVE_OFF&&lv_timer_get_paused(g_chat_ui.voice_wave_timer));
    review_capture(display,"voice-ended","语音小球 · 已结束");
    chat_ui_open_espclaw(NULL);assert(!g_chat_ui.ball_mode&&g_chat_ui.mode==AGENT_MODE_CREATE&&g_chat_ui.snapshot->count==0);
    frames(4); /* Capture the loaded native avatar, not its async placeholder. */
    review_capture(display,"mode-create","ESPClaw · 独立创作模式");
    chat_ui_modes(NULL);review_capture(display,"mode-selector","选择聊天、创作或助手");
    lv_obj_t *mode_box=lv_obj_get_child(g_chat_ui.mode_panel,0);
    lv_obj_send_event(lv_obj_get_child(mode_box,1),LV_EVENT_CLICKED,NULL);frames(4);
    assert(g_chat_ui.mode==AGENT_MODE_CHAT&&g_chat_ui.snapshot->count==3);
    assert(lv_obj_has_flag(g_chat_ui.preview_button,LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(g_chat_ui.snapshot->turns[0].prompt,"你好，小球"));
    review_capture(display,"espclaw-shared","ESPClaw · 共用语音对话记录");
    send_text("从 ESPClaw 继续");
    chat_ui_modes(NULL);assert(g_chat_ui.mode_panel);
    lv_obj_t *busy_modes=lv_obj_get_child(g_chat_ui.mode_panel,0);
    lv_obj_send_event(lv_obj_get_child(busy_modes,3),LV_EVENT_CLICKED,NULL);
    assert(g_chat_ui.mode==AGENT_MODE_CHAT);chat_ui_mode_close(NULL);
    review_chat_reply_fixture("接着聊。",true);frames(4);
    launch_builtin_ouo_qapp(NULL);frames(4);assert(g_chat_ui.snapshot->count==4);
    assert(!g_chat_ui.voice_session&&review_speech_count()==calls); /* No replay of history. */
    tap();review_voice_error_fixture();frames(4);assert(!g_chat_ui.voice_session);
    assert(glass_grok_current(g_chat_ui.ball)==34&&g_chat_ui.voice_wave==BALL_WAVE_OFF);
    review_capture(display,"voice-error","语音小球 · 识别失败可重试");
    /* A longer UTF-8 reply is spoken in order, without truncation or replay. */
    tap();review_voice_text_fixture("长回复验证");frames(4);
    char reply[2400];reply[0]=0;
    for(unsigned i=0;i<24;i++)strcat(reply,"这是一个较长的中文回答。内容按顺序播报。\n");
    char prior[256];snprintf(prior,sizeof(prior),"%s",review_speech_text());
    review_chat_reply_fixture(reply,true);frames(4);
    for(unsigned i=0;g_chat_ui.voice_phase==BALL_SPEAK&&i<30;i++){review_speech_done_fixture(0);frames(4);}
    assert(!strcmp(review_speech_text()+strlen(prior),reply));
    chat_ball_end(NULL);frames(4);
    tap();review_voice_text_fixture("模型缺失验证");frames(4);review_chat_reply_fixture("保存这段回复。",true);
    review_speech_failure_fixture(-ENOENT);frames(4);assert(!g_chat_ui.voice_session);
    assert(!strcmp(g_chat_ui.snapshot->turns[g_chat_ui.snapshot->count-1].reply,"保存这段回复。"));
    review_speech_failure_fixture(0);
    /* Async synthesis error halts the loop rather than recording over audio. */
    tap();review_voice_text_fixture("播报故障验证");frames(4);review_chat_reply_fixture("播报测试。",true);frames(4);
    review_speech_done_fixture(-ENOMEM);frames(4);assert(!g_chat_ui.voice_session);
    assert(glass_grok_current(g_chat_ui.ball)==34&&g_chat_ui.voice_wave==BALL_WAVE_OFF);
    tap();review_voice_text_fixture("AI 失败验证");frames(4);review_chat_error_fixture();frames(4);
    assert(!g_chat_ui.voice_session&&glass_grok_current(g_chat_ui.ball)==34&&g_chat_ui.voice_wave==BALL_WAVE_OFF);
    unsigned sends=review_chat_send_count();tap();panel_hide(NULL);review_voice_text_fixture("退出后的结果");frames(4);
    assert(!g_chat_ui.root&&review_chat_send_count()==sends);
    review_face_available_fixture(true);launch_builtin_ouo_qapp(NULL);
    assert(g_chat_ui.voice_face_request);cancels=review_face_cancel_count();panel_hide(NULL);
    assert(review_face_cancel_count()==cancels+1);review_face_available_fixture(false);
    launch_builtin_ouo_qapp(NULL);chat_ui_config(NULL);assert(chat_portal_return_ball);
    chat_ui_return_from_portal(NULL);assert(g_chat_ui.ball_mode&&!g_chat_ui.voice_session);
    panel_hide(NULL);glass_apps(NULL);launch_builtin_ouo_qapp(NULL);
    assert(g_desktop.chat_return_apps);glass_chat_back(NULL);assert(g_desktop.apps_page);
    for(unsigned i=0;i<30;i++){launch_builtin_ouo_qapp(NULL);panel_hide(NULL);}
    assert(!g_chat_ui.root);
    chat_interface_modes[0]=AGENT_MODE_ASSISTANT;glass_chat_show(false);frames(4);
    assert(g_chat_ui.mode==AGENT_MODE_ASSISTANT&&g_chat_ui.snapshot->count==0);
    assert(lv_obj_has_flag(g_chat_ui.preview_button,LV_OBJ_FLAG_HIDDEN));
    review_capture(display,"mode-assistant","ESPClaw · 独立助手模式");panel_hide(NULL);
  }
}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];
  int16_t pcm[]={0,-32768,32767,-1234};assert(glass_speech_pcm_level(pcm,4)==32767);
  assert(glass_speech_pcm_level(pcm,1)==0&&glass_speech_pcm_level(pcm+3,1)==1234);
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
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,GLASS_CONTENT_HEIGHT,0,0);
  lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  glass_edge_back_create();glass_status_create();
  run_voice_cases(display);
  review_chat_dialog_fixture(false);fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("GROK_VOICE_CHAT_OK: %u captures; shared ESPClaw; continuous voice, silence/send/TTS/drain/relisten/interrupt/errors/lifecycle\n",review_count);return 0;
}
