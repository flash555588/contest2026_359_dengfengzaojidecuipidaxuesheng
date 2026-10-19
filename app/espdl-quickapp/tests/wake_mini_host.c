/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <lvgl/lvgl.h>
#include "glass_grok.h"
#include "glass_voice.h"
#include "glass_speech.h"
#include "glass_chat.h"
static struct {bool light_theme;} g_desktop;
static struct {bool reduced_motion;} g_glass;
static struct {bool voice_session,speech_owned;} g_chat_ui;
static struct {lv_obj_t *root;} g_speech_ui;
static lv_font_t *font12,*font16;
static const lv_font_t *zh_font(int size){return size<=12?font12:font16;}
static struct speech_snapshot fixture_speech;
static struct glass_voice_snapshot fixture_voice;
static unsigned starts,stops,sends,arms;
static bool fixture_camera;
static bool fixture_command_pending;
static struct chat_activity_snapshot fixture_activity={.phase=CHAT_IDLE};
static bool fixture_result_enabled;
static struct chat_result_snapshot fixture_result;
static unsigned tts_calls,toasts,result_reads;static char spoken[241];
size_t glass_chat_copy_utf8(char *out,size_t size,const char *text)
{if(!size)return 0;size_t n=strlen(text);if(n>=size)n=size-1;while(n&&((unsigned char)text[n]&0xc0)==0x80)n--;memcpy(out,text,n);out[n]=0;return n;}
const char *glass_chat_error_text(enum chat_error error){(void)error;return "服务暂时不可用";}
bool glass_chat_result_get(struct chat_result_snapshot *out,uint32_t id,enum glass_agent_mode mode,size_t offset)
{result_reads++;assert(mode==AGENT_MODE_ASSISTANT);if(!fixture_result_enabled||(id&&id!=fixture_result.turn_id))return false;*out=fixture_result;size_t len=strlen(fixture_result.reply);size_t n=glass_chat_copy_utf8(out->reply,sizeof(out->reply),fixture_result.reply+(offset<=len?offset:len));out->next_offset=(uint32_t)(offset+n);out->more=out->next_offset<len;return true;}
int glass_speech_say_request(const char *text,uint32_t *request)
{tts_calls++;snprintf(spoken,sizeof(spoken),"%s",text);fixture_speech.active=true;fixture_speech.background_wake=false;fixture_speech.phase=SPEECH_SPEAKING;*request=++fixture_speech.request;return 0;}
static void show_toast(const char *text){assert(text&&*text);toasts++;}
void glass_speech_stop_request(uint32_t request)
{if(request&&request==fixture_speech.request)fixture_speech.active=false;}
static bool speech_ui_command_pending(void){return fixture_command_pending;}
bool qpk_runtime_camera_active(void){return fixture_camera;}
#include "qpk_wake_overlay.inc"
void glass_chat_activity_get(struct chat_activity_snapshot *out){*out=fixture_activity;}
void glass_chat_cancel(void){fixture_activity.phase=CHAT_IDLE;}
void glass_speech_get(struct speech_snapshot *out){*out=fixture_speech;}
void glass_voice_get(struct glass_voice_snapshot *out){*out=fixture_voice;}
int glass_voice_start(void){starts++;fixture_voice.active=true;fixture_voice.phase=GLASS_VOICE_LISTENING;fixture_voice.level=0;fixture_voice.text[0]=0;fixture_voice.revision++;return 0;}
void glass_voice_stop(void){stops++;fixture_voice.active=false;fixture_voice.phase=GLASS_VOICE_DONE;}
const char *glass_voice_error_text(enum glass_voice_error error)
{return error==GLASS_VOICE_ERROR_INCOMPLETE?"语音未完整识别，未发送，请重说":"语音识别失败";}
int glass_chat_send_mode(const char *text,enum glass_agent_mode mode)
{assert(text[0]&&mode==AGENT_MODE_ASSISTANT);sends++;fixture_activity.phase=CHAT_QUEUED;return 0;}
int glass_speech_background_wake(uint32_t *request)
{arms++;*request=fixture_speech.request=arms;fixture_speech.background_wake=true;fixture_speech.active=true;return 0;}
static bool fixture_pcm_busy;
static bool fixture_ball_route;
static bool chat_ball_is_present(void){return fixture_ball_route;}
static void chat_ball_show_result(uint32_t id){assert(fixture_ball_route&&id);}
static bool chat_ball_adopt_wake_capture(void)
{if(!fixture_ball_route)return false;g_chat_ui.voice_session=true;return true;}
static bool chat_ball_accept_wake(void)
{if(!fixture_ball_route)return false;g_chat_ui.voice_session=true;glass_voice_start_request(NULL);return true;}
bool music_pcm_session_active(void){return fixture_pcm_busy;}
#define GLASS_LOCAL_SPEECH 1
#include "glass_chat_result_ui.inc"
static unsigned mini_opacity_writes,mini_align_writes,mini_front_writes;
static void mini_set_opacity(lv_obj_t *object,lv_opa_t opacity,lv_style_selector_t selector)
{mini_opacity_writes++;lv_obj_set_style_opa(object,opacity,selector);}
static void mini_align(lv_obj_t *object,lv_align_t align,int32_t x,int32_t y)
{mini_align_writes++;lv_obj_align(object,align,x,y);}
static void mini_front(lv_obj_t *object)
{mini_front_writes++;lv_obj_move_foreground(object);}
#define lv_obj_set_style_opa mini_set_opacity
#define lv_obj_align mini_align
#define lv_obj_move_foreground mini_front
#include "glass_wake_mini.inc"
#undef lv_obj_set_style_opa
#undef lv_obj_align
#undef lv_obj_move_foreground
static unsigned char fb[1024*600*2];
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *pixels)
{for(int y=area->y1;y<=area->y2;y++){memcpy(fb+(y*1024+area->x1)*2,pixels,(area->x2-area->x1+1)*2);pixels+=(area->x2-area->x1+1)*2;}lv_display_flush_ready(d);}
static void capture(lv_display_t *display,const char *directory,const char *name)
{lv_refr_now(display);char path[1024];snprintf(path,sizeof(path),"%s/%s.rgb565",directory,name);FILE *file=fopen(path,"wb");assert(file);assert(fwrite(fb,1,sizeof(fb),file)==sizeof(fb));fclose(file);}
static void passthrough(lv_obj_t *obj)
{assert(!lv_obj_has_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE));for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)passthrough(lv_obj_get_child(obj,i));}
int main(int argc,char **argv)
{
  assert(argc==3);FILE *file=fopen(argv[1],"rb");assert(file);fseek(file,0,SEEK_END);size_t bytes=ftell(file);rewind(file);void *data=malloc(bytes);assert(data&&fread(data,1,bytes,file)==bytes);fclose(file);
  lv_init();lv_display_t *display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,fb,NULL,sizeof(fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);
  font12=lv_tiny_ttf_create_data(data,bytes,12);font16=lv_tiny_ttf_create_data(data,bytes,16);assert(font12&&font16);
  g_desktop.light_theme=true;g_glass.reduced_motion=true;
  /* An idle local-voice page and a waiting voice session do not own audio. */
  g_speech_ui.root=lv_obj_create(lv_screen_active());g_chat_ui.voice_session=true;
  /* Entering pages, manual ASR, arming and ordinary speech cannot summon. */
  wake_mini_service();assert(arms==1&&!g_wake_mini.visible);
  g_chat_ui.voice_session=false;
  fixture_speech.wake_ready=true;fixture_speech.wake_events=1;wake_mini_service();assert(!g_wake_mini.visible);
  fixture_speech.wake_event=1;fixture_speech.wake_ms=wake_mini_clock();fixture_speech.active=false;
  wake_mini_service();assert(g_wake_mini.visible&&starts==1);
  uint32_t shown=g_wake_mini.shown_at;
  lv_tick_inc(200);wake_mini_service();lv_obj_update_layout(lv_layer_top());
  lv_area_t area;lv_obj_get_coords(g_wake_mini.root,&area);assert(area.x1==16&&area.y1==472&&area.x2<1024&&area.y2<600);passthrough(g_wake_mini.root);
  for(int i=0;i<50&&!glass_grok_ready(g_wake_mini.ball);i++){struct timespec delay={.tv_nsec=10000000};nanosleep(&delay,NULL);glass_grok_test_step(g_wake_mini.ball);}
  assert(glass_grok_ready(g_wake_mini.ball));capture(display,argv[2],"light-mini");
  lv_obj_t *old=lv_screen_active(),*screen=lv_obj_create(NULL);lv_screen_load(screen);lv_obj_delete(old);
  wake_mini_service();assert(g_wake_mini.visible&&g_wake_mini.shown_at==shown&&lv_obj_is_visible(g_wake_mini.root));
  lv_obj_set_style_bg_color(screen,lv_color_hex(0x172131),0);capture(display,argv[2],"mini-over-new-screen");
  fixture_camera=true;wake_mini_service();assert(wake_overlay.data&&wake_mini_frame);
  uint16_t *camera=calloc(1024*600,sizeof(*camera));assert(camera);
  qpk_wake_overlay_blit(camera,1024,600,1024);assert(camera[500*1024+110]!=0&&camera[0]==0);
  free(camera);fixture_camera=false;wake_mini_camera();assert(!wake_overlay.data&&!wake_mini_frame);
  lv_tick_inc(5799);wake_mini_service();assert(g_wake_mini.visible);
  lv_tick_inc(1);wake_mini_service();assert(!g_wake_mini.visible&&stops==1&&starts==1);
  capture(display,argv[2],"mini-retired");
  for(int i=0;i<20;i++)wake_mini_service();assert(!g_wake_mini.visible);
  fixture_speech.active=false;fixture_speech.wake_event=2;fixture_speech.wake_ms=wake_mini_clock()-2000;wake_mini_service();assert(!g_wake_mini.visible);
  fixture_speech.active=false;fixture_speech.wake_event=3;fixture_speech.wake_ms=wake_mini_clock();g_chat_ui.voice_session=true;
  g_desktop.light_theme=false;wake_mini_service();assert(g_wake_mini.visible&&starts==2);capture(display,argv[2],"dark-mini");
  lv_tick_inc(3000);wake_mini_service();fixture_speech.wake_event=4;fixture_speech.wake_ms=wake_mini_clock();wake_mini_service();assert(g_wake_mini.shown_at==lv_tick_get());
  lv_tick_inc(5999);wake_mini_service();assert(g_wake_mini.visible);lv_tick_inc(1);wake_mini_service();assert(!g_wake_mini.visible);
  lv_tick_inc(1500);
  unsigned before=arms;fixture_speech.active=false;fixture_pcm_busy=true;wake_mini_service();assert(arms==before);
  fixture_pcm_busy=false;g_chat_ui.speech_owned=true;wake_mini_service();assert(arms==before);
  g_chat_ui.speech_owned=false;fixture_voice.active=true;wake_mini_service();assert(arms==before);
  fixture_voice.active=false;wake_mini_service();assert(arms==before+1);
  /* A fresh wake in somebody else's PCM gets its own request. */
  fixture_speech.active=false;fixture_voice.active=true;fixture_voice.phase=GLASS_VOICE_LISTENING;fixture_pcm_busy=true;
  fixture_speech.wake_event=5;fixture_speech.wake_ms=wake_mini_clock();
  unsigned started=starts,stopped=stops;wake_mini_service();
  assert(g_wake_mini.visible&&starts==started+1&&g_wake_mini.voice_owned);
  lv_tick_inc(6000);wake_mini_service();assert(!g_wake_mini.visible&&stops==stopped+1&&!fixture_voice.active);
  fixture_voice.active=false;fixture_pcm_busy=false;g_chat_ui.voice_session=false;
  fixture_ball_route=true;fixture_speech.request=g_wake_mini.request;
  fixture_speech.wake_event=6;fixture_speech.wake_ms=wake_mini_clock();
  wake_mini_service();assert(!g_wake_mini.visible&&starts==started+2&&g_chat_ui.voice_session&&!g_wake_mini.voice_owned);
  lv_tick_inc(6000);wake_mini_service();assert(!g_wake_mini.visible&&fixture_voice.active&&stops==stopped+1);
  /* Already listening on the large page must not summon a second ball. */
  fixture_speech.wake_event=7;fixture_speech.wake_ms=wake_mini_clock();wake_mini_service();
  assert(!g_wake_mini.visible&&starts==started+3&&stops==stopped+1);
  fixture_ball_route=false;g_chat_ui.voice_session=false;fixture_voice.active=false;
  fixture_speech.wake_event=8;fixture_speech.wake_ms=wake_mini_clock();wake_mini_service();
  assert(g_wake_mini.visible&&g_wake_mini.voice_owned);
  started=starts;fixture_ball_route=true;wake_mini_service();
  assert(!g_wake_mini.visible&&!g_wake_mini.voice_owned&&g_chat_ui.voice_session&&starts==started);
  /* Long commands, replies and device verification outlive six seconds.
   * Count inactivity only after their final completion, not from summon. */
  fixture_ball_route=false;fixture_voice.active=false;
  fixture_speech.active=false;fixture_speech.wake_event=9;fixture_speech.wake_ms=wake_mini_clock();
  fixture_activity.phase=CHAT_QUEUED;wake_mini_service();assert(g_wake_mini.visible);
  lv_tick_inc(12000);wake_mini_service();assert(g_wake_mini.visible&&g_wake_mini.busy);
  fixture_activity.phase=CHAT_REQUESTING;fixture_activity.activity=CHAT_ACTIVITY_TOOL;
  lv_tick_inc(20000);wake_mini_service();assert(g_wake_mini.visible);
  fixture_activity.phase=CHAT_IDLE;fixture_speech.active=true;fixture_speech.background_wake=false;fixture_speech.phase=SPEECH_SPEAKING;
  lv_tick_inc(20000);wake_mini_service();assert(g_wake_mini.visible);
  fixture_speech.phase=SPEECH_WAKE;fixture_command_pending=true;
  lv_tick_inc(12000);wake_mini_service();assert(g_wake_mini.visible);
  fixture_command_pending=false;wake_mini_service();assert(!g_wake_mini.busy);
  lv_tick_inc(5999);wake_mini_service();assert(g_wake_mini.visible);
  /* A fresh command restarts the whole idle window; a stale event does not. */
  fixture_speech.command_event=1;fixture_speech.command_ms=wake_mini_clock();wake_mini_service();
  lv_tick_inc(5999);wake_mini_service();assert(g_wake_mini.visible);
  fixture_speech.command_event=2;fixture_speech.command_ms=wake_mini_clock()-3000;wake_mini_service();
  lv_tick_inc(1);wake_mini_service();assert(!g_wake_mini.visible);
  fixture_speech.active=false;fixture_speech.request=g_wake_mini.request;
  fixture_speech.wake_event=10;fixture_speech.wake_ms=wake_mini_clock();
  g_chat_ui.voice_session=false;fixture_voice.active=false;wake_mini_service();
  assert(g_wake_mini.voice_owned&&fixture_voice.active);
  fixture_voice.level=1500;fixture_voice.revision++;wake_mini_service();
  unsigned before_stop=stops;lv_tick_inc(8000);wake_mini_service();
  assert(g_wake_mini.visible&&fixture_voice.active&&stops==before_stop);
  strcpy(fixture_voice.text,"打开客厅的灯");fixture_voice.revision++;fixture_voice.level=0;
  wake_mini_service();lv_tick_inc(1500);wake_mini_service();assert(stops==before_stop);
  lv_tick_inc(500);wake_mini_service();wake_mini_service();
  assert(sends==1&&fixture_activity.phase==CHAT_QUEUED&&g_wake_mini.visible);
  lv_tick_inc(15000);wake_mini_service();assert(g_wake_mini.visible);
  fixture_activity.phase=CHAT_IDLE;wake_mini_service();
  lv_tick_inc(5999);wake_mini_service();assert(g_wake_mini.visible);
  lv_tick_inc(1);wake_mini_service();assert(!g_wake_mini.visible);
  fixture_speech.active=false;fixture_speech.wake_event=11;fixture_speech.wake_ms=wake_mini_clock();
  fixture_voice.active=true;fixture_voice.phase=GLASS_VOICE_LISTENING;fixture_voice.level=1200;
  wake_mini_service();lv_tick_inc(10000);wake_mini_service();assert(g_wake_mini.visible);
  fixture_voice.level=0;fixture_voice.phase=GLASS_VOICE_FINISHING;
  lv_tick_inc(10000);wake_mini_service();assert(g_wake_mini.visible);
  fixture_voice.active=false;fixture_voice.phase=GLASS_VOICE_DONE;wake_mini_service();
  lv_tick_inc(5999);wake_mini_service();assert(g_wake_mini.visible);
  lv_tick_inc(1);wake_mini_service();assert(!g_wake_mini.visible);
  /* A received result owns one card/voice sequence, never a repeated action.
   * Confirmed and unconfirmed control receipts have distinct announcements. */
  fixture_result_enabled=true;fixture_result.turn_id=42;fixture_result.state=CHAT_WAITING;
  fixture_result.card_count=1;strcpy(fixture_result.cards[0].name,"客厅主灯");
  strcpy(fixture_result.cards[0].state,"已开启");strcpy(fixture_result.cards[0].kind,"light");
  fixture_result.cards[0].action=fixture_result.cards[0].verified=true;fixture_result.cards[0].level=30;
  fixture_speech.active=false;fixture_voice.active=false;fixture_activity.phase=CHAT_QUEUED;
  fixture_speech.wake_event=12;wake_mini_present(12,lv_tick_get());g_wake_mini.turn=42;g_wake_mini.result_pending=true;g_wake_mini.speech_cursor=0;
  wake_mini_service();assert(g_wake_mini.result_root&&!tts_calls);
  fixture_result.state=CHAT_DONE;fixture_activity.phase=CHAT_IDLE;
  wake_mini_service();assert(tts_calls==1&&strstr(spoken,"客厅主灯")&&strstr(spoken,"已开启")&&g_wake_mini.speech_owned);
  for(unsigned i=0;i<8;i++){lv_tick_inc(1000);wake_mini_service();}
  assert(tts_calls==1&&g_wake_mini.visible);
  capture(display,argv[2],"verified-result");
  fixture_speech.active=false;wake_mini_service();assert(!g_wake_mini.result_pending);
  lv_tick_inc(5999);wake_mini_service();assert(g_wake_mini.visible);lv_tick_inc(1);wake_mini_service();assert(!g_wake_mini.visible);
  fixture_result.turn_id=43;fixture_result.cards[0].verified=false;
  strcpy(fixture_result.reply,"操作成功，已经完成。");
  fixture_speech.wake_event=13;wake_mini_present(13,lv_tick_get());g_wake_mini.turn=43;g_wake_mini.result_pending=true;g_wake_mini.speech_cursor=0;
  wake_mini_service();assert(tts_calls==2&&strstr(spoken,"没有确认")&&!strstr(spoken,"成功"));
  capture(display,argv[2],"unconfirmed-result");
  fixture_camera=true;lv_tick_inc(100);wake_mini_service();assert(wake_overlay.height>120&&wake_overlay.height<=360);
  fixture_camera=false;wake_mini_camera();
  fixture_speech.active=false;fixture_speech.error=-EIO;wake_mini_service();
  assert(!g_wake_mini.result_pending&&toasts==1);
  fixture_speech.error=0;fixture_result.cards[0].verified=true;fixture_result.turn_id=44;
  fixture_speech.wake_event=14;wake_mini_present(14,lv_tick_get());
  g_wake_mini.turn=44;g_wake_mini.result_pending=true;g_wake_mini.speech_cursor=0;
  wake_mini_service();assert(tts_calls==3&&g_wake_mini.speech_owned);
  fixture_result_enabled=false;wake_mini_service();
  assert(!g_wake_mini.speech_owned&&!g_wake_mini.result_pending&&!g_wake_mini.result_root);
  fixture_result_enabled=true;fixture_result.turn_id=45;fixture_result.card_count=0;
  fixture_speech.error=0;fixture_speech.active=false;fixture_speech.wake_event=15;
  wake_mini_present(15,lv_tick_get());g_wake_mini.turn=45;g_wake_mini.result_pending=true;g_wake_mini.speech_cursor=0;
  strcpy(fixture_result.reply,"请说明完整设备名。");
  wake_mini_service();assert(!strcmp(spoken,fixture_result.reply));
  assert(!strcmp(chat_result_title(&fixture_result),"AI 回复"));capture(display,argv[2],"plain-reply");
  lv_obj_delete(g_wake_mini.root);lv_display_delete(display);lv_tiny_ttf_destroy(font12);lv_tiny_ttf_destroy(font16);lv_deinit();free(data);
  uint8_t rgba[8]={0,0,255,255,255,0,0,0};uint16_t pixels[4]={0,0x07e0,0,0};
  assert(!qpk_runtime_wake_overlay(rgba,2,1,8,0,0));qpk_wake_overlay_blit(pixels,2,2,2);assert(pixels[0]==0xf800&&pixels[1]==0x07e0&&pixels[2]==0);
  assert(!qpk_runtime_wake_overlay(rgba,2,1,8,-1,-1));qpk_wake_overlay_blit(pixels,2,2,2);
  assert(qpk_runtime_wake_overlay(rgba,401,1,8,0,0)==-EINVAL);qpk_runtime_wake_overlay(NULL,0,0,0,0,0);
  puts("WAKE_MINI_OK real-event only; large page owns presentation; 6000ms idle only; long speech, ASR finishing, AI/tool and device verification retained; fresh/stale commands; background capture; screen switch; pass-through; real SD player; light/dark");return 0;
}

int glass_voice_start_request(uint32_t *request){int ret=glass_voice_start();if(!ret){if(!++fixture_voice.request)fixture_voice.request=1;if(request)*request=fixture_voice.request;}return ret;}
void glass_voice_stop_request(uint32_t request){if(request&&request==fixture_voice.request)glass_voice_stop();}
void glass_voice_cancel(void){glass_voice_stop();fixture_voice.active=false;fixture_voice.text[0]=0;fixture_voice.phase=GLASS_VOICE_IDLE;fixture_voice.request++;}
