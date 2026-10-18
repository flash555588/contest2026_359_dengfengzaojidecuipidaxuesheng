/* SPDX-License-Identifier: Apache-2.0 */
#define MUSIC_FULL_NO_MAIN
#define MUSIC_PV_SD_BANKS
#include "render_music_full.c"
#include <unistd.h>
static void await_sd(struct music_pv_ui *p)
{
  for(unsigned n=0;n<3000;n++){
    music_pv_refresh(p);
    if(p->sd_present&&p->sd&&p->sd->has_frame)return;
    assert(!p->sd_failed);usleep(1000);
  }
  assert(!"SD worker timed out");
}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
  void *font=malloc(size);assert(font&&fread(font,1,size,f)==size);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(font,size,16);g_desktop.font20=lv_tiny_ttf_create_data(font,size,20);g_desktop.font28=lv_tiny_ttf_create_data(font,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(font,size,48);glass_home_fonts_init(font,size);g_glass.font72=lv_tiny_ttf_create_data(font,size,72);
  g_desktop.screen=glass_box(lv_screen_active(),0,32,1024,568,0,0);lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,568,0,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  glass_edge_back_create();glass_status_create();glass_rebuild();review_music_fixture(2);
  struct music_now_playing now={.track_id=100,.position_ms=1100,.duration_ms=8000,.state=MUSIC_PAUSED,.lyrics_state=MUSIC_LYRICS_READY};
  strcpy(now.name,"慢下来的光");strcpy(now.artist,"原创演示");review_music_now_fixture(&now);review_lyrics_fixture(4,0);
  glass_music(NULL);music_full_open(NULL);music_full_mode(NULL);struct music_pv_ui *p=&g_music_ui.full->pv;
  assert(p->assets.count==27);
  for(unsigned style=0;style<27;style++){
    await_sd(p);assert(p->sd->shown==13);unsigned reads=p->sd->reads;
    for(unsigned i=0;i<30;i++){lv_tick_inc(40);music_pv_refresh(p);usleep(1000);}
    assert(p->sd->reads==reads&&p->sd->shown==13);
    snprintf(path,sizeof(path),"sd-bank-%02u",style);review_capture(display,path,"完整版 JIZURA · SD 帧库");
    if(style<26)lv_obj_send_event(p->style_button,LV_EVENT_CLICKED,NULL);
  }
  now.position_ms=5300;review_music_now_fixture(&now);music_pv_refresh(p);assert(!p->sd_present);await_sd(p);assert(p->sd->shown==63);
  now.position_ms=400;review_music_now_fixture(&now);music_pv_refresh(p);assert(!p->sd_present);await_sd(p);assert(p->sd->shown==4);
  /* Playing seeks request the current frame; a fractional frame boundary
   * must prefetch frame 2 at 166 ms rather than skipping directly to 3. */
  now.state=MUSIC_PLAYING;now.position_ms=166;review_music_now_fixture(&now);await_sd(p);assert(p->sd->shown==1);
  bool prepared=false;
  for(unsigned n=0;n<3000&&!prepared;n++){
    music_pv_refresh(p);pthread_mutex_lock(&p->sd->lock);
    prepared=p->sd->ready;unsigned frame=p->sd->frame;pthread_mutex_unlock(&p->sd->lock);
    if(prepared)assert(frame==2);else usleep(1000);
  }
  assert(prepared&&p->sd->shown==1);
  now.position_ms=167;review_music_now_fixture(&now);music_pv_refresh(p);assert(p->sd->shown==2);
  now.state=MUSIC_PAUSED;now.position_ms=400;review_music_now_fixture(&now);
  for(unsigned n=0;n<3000&&p->sd->shown!=4;n++){music_pv_refresh(p);usleep(1000);}
  assert(p->sd->shown==4);
  g_glass.reduced_motion=true;music_pv_refresh(p);assert(!p->sd&&!p->sd_present);g_glass.reduced_motion=false;
  now.track_id++;strcpy(now.name,"没有素材的歌曲");review_music_now_fixture(&now);
  for(unsigned n=0;n<1000&&!p->sd_failed;n++){music_pv_refresh(p);usleep(1000);}
  assert(p->sd_failed&&!p->sd&&!p->sd_present);music_full_close(NULL);
  for(unsigned n=0;n<60;n++){
    now.track_id++;strcpy(now.name,"慢下来的光");now.position_ms=1100;review_music_now_fixture(&now);
    music_full_open(NULL);music_full_mode(NULL);await_sd(&g_music_ui.full->pv);music_full_close(NULL);assert(!g_music_ui.full);
  }
  panel_hide(NULL);fputs("\n]\n",review_manifest);fclose(review_manifest);
  puts("MUSIC_SD_UI_OK: 27 full-render banks; paused reads stop; bidirectional seek; missing bank fallback; reduced motion; 60 worker/buffer teardown cycles");return 0;
}
