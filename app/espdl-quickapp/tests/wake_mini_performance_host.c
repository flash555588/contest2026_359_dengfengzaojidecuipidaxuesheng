/* SPDX-License-Identifier: Apache-2.0 */
/* Reuse native LVGL/service fixtures, without replaying old UI scenarios. */
#define main wake_mini_reference_main
#include "wake_mini_host.c"
#undef main
int main(int argc,char **argv)
{
  assert(argc==3);FILE *file=fopen(argv[1],"rb");assert(file);
  fseek(file,0,SEEK_END);size_t bytes=ftell(file);rewind(file);
  void *data=malloc(bytes);assert(data&&fread(data,1,bytes,file)==bytes);fclose(file);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,fb,NULL,sizeof(fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,flush);
  font12=lv_tiny_ttf_create_data(data,bytes,12);font16=lv_tiny_ttf_create_data(data,bytes,16);
  assert(font12&&font16);g_desktop.light_theme=true;g_glass.reduced_motion=true;
  fixture_result_enabled=true;fixture_result.turn_id=42;fixture_result.state=CHAT_DONE;
  strcpy(fixture_result.reply,"操作结果已保留。");
  fixture_speech.wake_event=1;fixture_speech.active=true;fixture_speech.phase=SPEECH_SPEAKING;
  fixture_activity.phase=CHAT_QUEUED;
  wake_mini_present(1,lv_tick_get());g_wake_mini.turn=42;
  wake_mini_service();glass_grok_set_active(g_wake_mini.ball,false);
  unsigned opacity=mini_opacity_writes,align=mini_align_writes,front=mini_front_writes;
  for(unsigned i=0;i<1000;i++)wake_mini_service();
  assert(mini_opacity_writes==opacity&&mini_align_writes==align&&mini_front_writes==front);
  puts("WAKE_MINI_PERF stationary_loops=1000 opacity_writes=0 align_writes=0 front_writes=0");
  /* Going to large ball hides the overlay, but must finish its pending TTS. */
  fixture_ball_route=true;fixture_activity.phase=CHAT_IDLE;fixture_speech.active=false;
  g_wake_mini.result_pending=true;g_wake_mini.speech_cursor=0;
  wake_mini_service();assert(!g_wake_mini.visible&&g_wake_mini.speech_owned&&tts_calls==1);
  fixture_speech.active=false;wake_mini_service();
  assert(!g_wake_mini.result_pending&&!g_wake_mini.speech_owned);
  unsigned reads=result_reads;
  for(unsigned i=0;i<1000;i++)wake_mini_service();
  assert(reads==result_reads&&tts_calls==1);
  puts("WAKE_MINI_PERF hidden_completed_loops=1000 result_reads=0; hidden_pending_tts=completed_once");
  /* The optimization introduces no timer gate on wake or request preemption. */
  fixture_ball_route=false;fixture_speech.active=false;
  fixture_speech.wake_event=2;fixture_speech.wake_ms=wake_mini_clock();
  wake_mini_service();assert(g_wake_mini.visible&&g_wake_mini.voice_owned&&starts==1);
  fixture_voice.request++;wake_mini_service();
  assert(!g_wake_mini.voice_owned&&fixture_voice.active&&!sends);
  lv_obj_delete(g_wake_mini.root);lv_display_delete(display);
  lv_tiny_ttf_destroy(font12);lv_tiny_ttf_destroy(font16);lv_deinit();free(data);
  puts("WAKE_MINI_PERF_OK: stable paint, hidden completion, fresh wake and latest owner");
  return 0;
}
