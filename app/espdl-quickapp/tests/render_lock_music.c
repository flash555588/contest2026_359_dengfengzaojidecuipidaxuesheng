/* SPDX-License-Identifier: Apache-2.0 */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_unified_devices.c"
extern void review_music_now_fixture(const struct music_now_playing *);
extern void review_lock_photo_fixture(int);
extern void review_cover_fixture(const char *);
static lv_point_t touch_point;
static lv_indev_state_t touch_state;
static unsigned leaked_touches;
static void touch_read(lv_indev_t *input,lv_indev_data_t *data)
{(void)input;data->point=touch_point;data->state=touch_state;}
static void touch(lv_indev_t *input,int x,int y,bool pressed)
{touch_point=(lv_point_t){x,y};touch_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(input);}
static void underlying_click(lv_event_t *e){(void)e;leaked_touches++;}
static unsigned lock_object_count(lv_obj_t *root)
{unsigned count=1;for(unsigned i=0;i<lv_obj_get_child_count(root);i++)count+=lock_object_count(lv_obj_get_child(root,i));return count;}
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




  g_glass.font72=lv_tiny_ttf_create_data(font,size,72);
  lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,touch_read);lv_indev_set_display(input,display);
  g_desktop.light_theme=false;glass_rebuild();review_music_fixture(2);
  struct music_now_playing now={.revision=41,.position_ms=42000,.duration_ms=204000,.volume=40,
    .state=MUSIC_PLAYING,.lyrics_state=MUSIC_LYRICS_READY,.can_skip=true,.lyrics_started=true};
  strcpy(now.name,"城市与晚风");strcpy(now.artist,"声动 · 原创演示");strcpy(now.album,"此刻的声音");
  strcpy(now.lyrics[0],"沿着街灯慢慢走");strcpy(now.lyrics[1],"把今天交给温柔的晚风");strcpy(now.lyrics[2],"下一站还有新的梦");
  review_music_now_fixture(&now);glass_music(NULL);lv_obj_t *page=g_desktop.current_card;
  review_capture(display,"music-lyrics","音乐后台与歌词入口");
  lv_obj_t *entry=review_callback_find(page,glass_lock_open);assert(entry);lv_obj_send_event(entry,LV_EVENT_CLICKED,NULL);
  assert(g_lock_ui.root && g_desktop.current_card==page);assert(lv_timer_get_paused(g_music_ui.timer));
  assert(lock_object_count(g_lock_ui.root)==37);
  review_capture(display,"lock-playing","锁屏 · 同步歌词（原创演示）");
  /* Unchanged state must not invalidate/reallocate label strings. */
  lv_obj_t *title=g_lock_ui.title;const char *buffer=lv_label_get_text(title);lock_refresh(NULL);assert(buffer==lv_label_get_text(title));
  lock_toggle(NULL);lock_refresh(NULL);assert(!strcmp(lv_label_get_text(g_lock_ui.status),"已暂停"));
  review_capture(display,"lock-paused","锁屏 · 暂停");lock_toggle(NULL);
  lock_unlock(NULL);assert(!g_lock_ui.root && g_desktop.current_card==page);assert(!lv_timer_get_paused(g_music_ui.timer));
  panel_hide(NULL);struct music_now_playing after;glass_music_now_playing(&after);assert(after.state==MUSIC_PLAYING && after.position_ms==42000);
  lv_obj_send_event(g_glass.clock,LV_EVENT_CLICKED,NULL);assert(g_lock_ui.root);
  lv_obj_send_event(g_lock_ui.next,LV_EVENT_CLICKED,NULL);
  lv_obj_update_layout(g_lock_ui.root);
  assert(!strcmp(lv_label_get_text(g_lock_ui.title),"下一首示例"));assert(!strcmp(lv_label_get_text(g_lock_ui.lyric[1]),"正在获取歌词…"));
  now.revision=50;now.lyrics_state=MUSIC_LYRICS_MISSING;memset(now.lyrics,0,sizeof(now.lyrics));review_music_now_fixture(&now);lock_refresh(NULL);
  assert(!strcmp(lv_label_get_text(g_lock_ui.lyric[1]),"暂无歌词"));review_capture(display,"lock-no-lyrics","锁屏 · 无歌词");
  now.revision++;now.state=MUSIC_ERROR;review_music_now_fixture(&now);lock_refresh(NULL);review_capture(display,"lock-error","锁屏 · 网络播放失败");
  lock_unlock(NULL);memset(&now,0,sizeof(now));now.revision=60;review_music_now_fixture(&now);
  glass_lock_open(NULL);review_capture(display,"lock-idle","锁屏 · 未播放音乐");lock_unlock(NULL);
  lv_obj_t *under=glass_box(g_desktop.screen,700,520,100,60,0xffffff,0);glass_clickable(under,underlying_click,NULL);
  glass_lock_open(NULL);lv_obj_update_layout(lv_layer_top());
  touch(input,740,550,true);touch(input,740,550,false);assert(!leaked_touches && g_lock_ui.root);
  touch(input,740,550,true);touch(input,740,510,true);touch(input,740,440,true);touch(input,740,350,true);touch(input,740,350,false);
  assert(!g_lock_ui.root && !leaked_touches);lv_obj_delete(under);
  assert(lock_month_days(2024,1)==29 && lock_month_days(2000,1)==29 && lock_month_days(1900,1)==28 && lock_month_days(2026,3)==30);
  glass_lock_open(NULL);lock_clock_update(1709164800);assert(g_lock_ui.local.tm_mon==1 && g_lock_ui.local.tm_mday==29);
  lock_clock_update(1709251200);assert(g_lock_ui.local.tm_mon==2 && g_lock_ui.local.tm_mday==1);
  lock_clock_update(0);assert(!g_lock_ui.time_valid && lv_obj_has_flag(g_lock_ui.hand[0],LV_OBJ_FLAG_HIDDEN));
  lock_unlock(NULL);
  glass_lock_open(NULL);lv_obj_update_layout(lv_layer_top());lv_obj_t *pager=review_callback_find(g_lock_ui.root,glass_pager_deleted);assert(pager);struct glass_pager *pager_state=lv_obj_get_user_data(pager);lv_obj_send_event(pager_state->slots[1].hit,LV_EVENT_CLICKED,NULL);assert(g_lock_page_pending);lv_async_call_cancel(lock_page_async,NULL);lock_page_async(NULL);
  assert(g_lock_page==1 && g_lock_ui.elapsed);strcpy(now.name,"城市与晚风");strcpy(now.artist,"声动 · 原创演示");strcpy(now.lyrics[1],"把今天交给温柔的晚风");strcpy(now.lyrics[2],"下一站还有新的梦");
  now.state=MUSIC_PLAYING;now.lyrics_state=MUSIC_LYRICS_READY;now.revision=80;now.duration_ms=204000;now.position_ms=42000;now.can_skip=true;
  review_music_now_fixture(&now);lock_refresh(NULL);
  review_capture(display,"lock-music-page","待机音乐页 · 后台播放与歌词");
  lock_page_set(2);lv_async_call_cancel(lock_page_async,NULL);lock_page_async(NULL);review_weather_fixture(1);lock_refresh(NULL);lv_obj_update_layout(g_lock_ui.root);
  assert(review_label_find(g_lock_ui.root,"多云"));review_capture(display,"lock-weather-page","待机天气页 · 现有服务数据示例");
  review_weather_fixture(0);lock_refresh(NULL);lv_obj_update_layout(g_lock_ui.root);assert(review_label_find(g_lock_ui.root,"联网后自动获取天气"));
  review_capture(display,"lock-weather-empty","待机天气页 · 等待联网");
  review_lock_photo_fixture(0);lock_page_set(3);lv_async_call_cancel(lock_page_async,NULL);lock_page_async(NULL);lock_refresh(NULL);
  assert(strstr(lv_label_get_text(g_lock_ui.photo_status),"/sdcard/lockscreen/photo.jpg"));
  review_capture(display,"lock-photo-missing","待机照片页 · 缺少 SD 照片");
  lock_unlock(NULL);review_lock_photo_fixture(1);glass_lock_open(NULL);lock_refresh(NULL);
  assert(lv_obj_has_flag(g_lock_ui.photo_status,LV_OBJ_FLAG_HIDDEN));review_capture(display,"lock-photo-demo","待机照片页 · 离线合成测试图像");
  lock_unlock(NULL);g_lock_page=1;
  char cover_path[1024];snprintf(cover_path,sizeof(cover_path),"%s/cover.rgb565",review_out);review_cover_fixture(cover_path);
  strcpy(now.name,"海阔天空");strcpy(now.artist,"Beyond");strcpy(now.cover_url,"https://fixture/online-cover.jpg");now.track_id=100;now.revision=100;
  strcpy(now.lyrics[1],"歌词接口已匹配");strcpy(now.lyrics[2],"按实际播放进度同步显示");review_music_now_fixture(&now);
  glass_music(NULL);assert(g_music_ui.cover.canvas);review_capture(display,"music-online-cover","音乐应用 · 实际 API 封面");glass_lock_open(NULL);assert(g_lock_ui.cover.canvas);
  review_capture(display,"lock-online-cover","封面 API 接入 · 实际下载与解码；歌词为验证提示");
  /* Clear stale art even if the next song has the same URL or lacks metadata. */
  now.track_id++;now.revision++;now.cover_url[0]=0;review_music_now_fixture(&now);lock_refresh(NULL);assert(!g_lock_ui.cover.canvas);
  lock_unlock(NULL);music_ui_refresh(NULL);assert(!g_music_ui.cover.canvas);
  /* A completed image must appear while playback's revision is unchanged. */
  strcpy(now.cover_url,"https://fixture/online-cover.jpg");now.revision++;review_music_now_fixture(&now);review_cover_fixture(NULL);music_ui_refresh(NULL);assert(!g_music_ui.cover.canvas);
  review_cover_fixture(cover_path);music_ui_refresh(NULL);assert(g_music_ui.cover.canvas);
  g_lock_page=3;glass_lock_open(NULL);
  /* Pending page transitions cannot reopen a screen after unlocking. */
  lock_page_set(0);assert(g_lock_page_pending);lock_unlock(NULL);assert(!g_lock_page_pending && !g_lock_ui.root);g_lock_page=0;
  /* Repeated lifecycle changes must not leave stale UI pointers/timers. */
  for(unsigned i=0;i<30;i++){glass_music(NULL);glass_lock_open(NULL);lock_unlock(NULL);panel_hide(NULL);}
  assert(!g_lock_ui.root && !g_music_ui.root);
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  puts("LOCK_MUSIC_NATIVE_OK: 13 captures, page preserved, background state, pause/resume, skip clears old lyrics, missing/error/idle, touch isolation/swipe unlock, 37 clock-page objects, leap-year/month rollover/invalid clock four exclusive pages, photo/cover canvas teardown, stale artwork clearing and paused-revision image completion, transition cancellation and 30 reopen cycles");return 0;
}
