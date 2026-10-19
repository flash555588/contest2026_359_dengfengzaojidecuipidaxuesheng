/* The old broad review remains available; exercise only the new ownership path. */
#define main previous_mini_review
#include "wake_mini_host.c"
#undef main
int main(int argc,char **argv)
{
  assert(argc==3);FILE *file=fopen(argv[1],"rb");assert(file);fseek(file,0,SEEK_END);size_t bytes=ftell(file);rewind(file);
  void *data=malloc(bytes);assert(data&&fread(data,1,bytes,file)==bytes);fclose(file);
  lv_init();lv_display_t *display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,fb,NULL,sizeof(fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);
  font12=lv_tiny_ttf_create_data(data,bytes,12);font16=lv_tiny_ttf_create_data(data,bytes,16);assert(font12&&font16);
  g_glass.reduced_motion=true;
  wake_mini_service();assert(arms==1&&!g_wake_mini.visible);
  /* A wake observed in another page's PCM must obtain a new request. */
  fixture_voice.active=true;fixture_voice.phase=GLASS_VOICE_LISTENING;fixture_voice.request=100;
  fixture_speech.active=false;fixture_speech.wake_event=1;fixture_speech.wake_ms=wake_mini_clock();
  wake_mini_service();assert(g_wake_mini.visible&&g_wake_mini.voice_owned&&starts==1);
  assert(g_wake_mini.voice_request==101&&fixture_voice.request==101);
  /* A later manual input wins. Late mini completion must not send its text. */
  fixture_voice.request=102;fixture_voice.active=false;fixture_voice.phase=GLASS_VOICE_DONE;strcpy(fixture_voice.text,"belongs to another input");
  wake_mini_service();assert(!g_wake_mini.voice_owned&&sends==0);
  fixture_voice.text[0]=0;fixture_speech.wake_event=2;fixture_speech.wake_ms=wake_mini_clock();
  wake_mini_service();assert(starts==2&&g_wake_mini.voice_owned&&g_wake_mini.voice_request==103);
  /* The matching final transcript is sent once, with its full negation. */
  fixture_voice.active=false;fixture_voice.phase=GLASS_VOICE_DONE;strcpy(fixture_voice.text,"只查询客厅灯状态，不要开灯。");fixture_voice.revision++;
  wake_mini_service();assert(sends==1&&!g_wake_mini.voice_owned);wake_mini_service();assert(sends==1);
  /* A fresh wake on the large page keeps using that element. */
  fixture_ball_route=true;fixture_speech.wake_event=3;fixture_speech.wake_ms=wake_mini_clock();
  wake_mini_service();assert(!g_wake_mini.visible&&g_chat_ui.voice_session&&starts==3&&!g_wake_mini.voice_owned);
  puts("WAKE_MINI_PREEMPTION_OK: wake takes other-page PCM, later manual input wins, stale final never sent, matching transcript once, large-ball routing");
  if(g_wake_mini.root){glass_grok_set_active(g_wake_mini.ball,false);lv_obj_delete(g_wake_mini.root);g_wake_mini.root=NULL;}
  lv_display_delete(display);lv_font_t *a=font12,*b=font16;lv_tiny_ttf_destroy(a);lv_tiny_ttf_destroy(b);lv_deinit();free(data);return 0;
}
