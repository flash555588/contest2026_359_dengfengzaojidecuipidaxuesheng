/* SPDX-License-Identifier: Apache-2.0 */
/* Actual app layout, simulated transport. This is not a board screenshot. */
#include "bili_live.c"
#include <assert.h>
#include <lvgl/src/misc/lv_timer_private.h>
static uint8_t fb[1024 * 600 * 2];
static void flush(lv_display_t *disp,const lv_area_t *area,uint8_t *p)
{
  for (int y=area->y1;y<=area->y2;y++) {
    memcpy(fb+(y*1024+area->x1)*2,p,(area->x2-area->x1+1)*2);
    p+=(area->x2-area->x1+1)*2;
  }
  lv_display_flush_ready(disp);
}
struct bili_http *bili_http_open(const char *u,const char *c,atomic_bool *a,int *e)
{ (void)u;(void)c;(void)a;*e=-ECANCELED;return NULL; }
struct bili_http *bili_http_open_media(const char *u,const char *c,unsigned long id,atomic_bool *a,int *e)
{ (void)id;return bili_http_open(u,c,a,e); }
static atomic_bool hold_http, entered_http;
int bili_http_json(const char *u,const char *c,atomic_bool *a,char **j)
{ (void)u;(void)c;(void)a;
  atomic_store(&entered_http,true);
  while (atomic_load(&hold_http)) usleep(1000);
  *j=NULL;return -ECANCELED; }
struct bili_http *bili_http_open_site(const char *u,const char *c,const char *ref,atomic_bool *a,int *e)
{(void)ref;return bili_http_open(u,c,a,e);}
int bili_http_json_site(const char *u,const char *c,const char *ref,atomic_bool *a,char **j)
{(void)ref;return bili_http_json(u,c,a,j);}
int bili_http_read(struct bili_http *h,uint8_t *p,size_t n)
{ (void)h;(void)p;(void)n;return -ECANCELED; }
void bili_http_close(struct bili_http *h) { (void)h; }
void *bili_decoder_create(bili_frame_fn f,void *p) { (void)f;(void)p;return NULL; }
void *bili_decoder_create_timed(bili_timed_frame_fn f,void *p){(void)f;(void)p;return NULL;}
int bili_decoder_feed_timestamp(void *d,const uint8_t *p,size_t n,uint64_t t){(void)d;(void)p;(void)n;(void)t;return -ENOTSUP;}
int bili_decoder_feed(void *d,const uint8_t *p,size_t n) { (void)d;(void)p;(void)n;return -ENOTSUP; }
void bili_decoder_destroy(void *d) { (void)d; }
const char *live_platform_name(int p){return p==LIVE_HUYA?"虎牙":p==LIVE_CC?"网易CC":"B站";}
int live_resolve(int platform,const char *room,const char *cookie,atomic_bool *cancel,struct bili_streams *out)
{(void)platform;(void)room;(void)out;char *j=NULL;return bili_http_json("",cookie,cancel,&j);}
static void pump(void)
{
  lv_tick_inc(110); lv_timer_handler(); usleep(1000);
}
int main(int argc,char **argv)
{
  assert(argc==3);
  struct session pixels={0};
  uint8_t Y[4]={235,235,235,235},U[1]={128},V[1]={128};
  const uint8_t *planes[3]={Y,U,V};int strides[2]={2,1};
  atomic_init(&pixels.cancel,false);atomic_init(&pixels.scale,true);pthread_mutex_init(&pixels.lock,NULL);
  pixels.pending=calloc(CAPACITY_PIXELS,2);pixels.shown=calloc(CAPACITY_PIXELS,2);pixels.back=calloc(CAPACITY_PIXELS,2);
  assert(pixels.pending && pixels.shown && pixels.back);
  assert(frame(&pixels,planes,strides,2,2)==0);
  assert(pixels.pending[180*WIDTH+320]==0xffff && pixels.pending[0]==0);
  assert(pixels.frames==1 && pixels.dirty);
  atomic_store(&pixels.cancel,true);assert(frame(&pixels,planes,strides,2,2)==-ECANCELED);
  free(pixels.pending);free(pixels.shown);free(pixels.back);pthread_mutex_destroy(&pixels.lock);
  FILE *fp=fopen(argv[1],"rb");assert(fp);
  fseek(fp,0,SEEK_END);long n=ftell(fp);rewind(fp);
  uint8_t *fontdata=malloc(n);assert(fontdata);
  assert(fread(fontdata,1,n,fp)==(size_t)n);fclose(fp);
  lv_init();
  lv_font_t *font=lv_tiny_ttf_create_data(fontdata,n,18);assert(font);
  lv_display_t *display=lv_display_create(1024,600);assert(display);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,fb,NULL,sizeof(fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,flush);
  lv_obj_t *screen=lv_screen_active();
  lv_obj_set_style_bg_color(screen,lv_color_hex(0x15202b),0);
  lv_obj_t *title=lv_label_create(screen);
  lv_obj_set_style_text_font(title,font,0);
  lv_obj_set_style_text_color(title,lv_color_white(),0);
  lv_label_set_text(title,"‹  网络直播");lv_obj_set_pos(title,22,22);
  lv_obj_t *page=lv_obj_create(screen);
  lv_obj_set_pos(page,0,64);lv_obj_set_size(page,1024,536);
  bili_live_show(page,font);
  pump();
  assert(g_timer->period==33);
  lv_obj_remove_state(g_scale_switch,LV_STATE_CHECKED);lv_obj_send_event(g_scale_switch,LV_EVENT_VALUE_CHANGED,NULL);
  assert(!g_scale&&g_fullscreen&&lv_obj_get_parent(g_image)==g_fullscreen);
  lv_obj_send_event(lv_obj_get_child(g_fullscreen,1),LV_EVENT_CLICKED,NULL);
  assert(g_scale&&!g_fullscreen&&lv_obj_get_parent(g_image)==g_viewport&&lv_obj_has_state(g_scale_switch,LV_STATE_CHECKED));
  lv_area_t r;lv_obj_get_coords(g_room,&r);
  assert(r.x1>=0 && r.x2<1024 && r.y1>=64 && r.y2<600);
  lv_obj_t *keyboard=lv_obj_get_child(page,-1);
  lv_obj_get_coords(keyboard,&r);
  assert(r.x1>=0 && r.x2<1024 && r.y1>=64 && r.y2<600);
  lv_textarea_set_text(g_room,"12345");
  lv_refr_now(display);
  fp=fopen(argv[2],"wb");assert(fp);assert(fwrite(fb,1,sizeof(fb),fp)==sizeof(fb));fclose(fp);
  /* Hold the old worker until the UI has queued a replacement. */
  for (int mode=0;mode<3;mode++) {
    atomic_store(&hold_http,true);atomic_store(&entered_http,false);
    bili_live_play_room("12345");
    for (int j=0;j<1000 && !atomic_load(&entered_http);j++) pump();
    assert(atomic_load(&entered_http));
    live_mode_set(false);assert(g_fullscreen&&!atomic_load(&g_session->scale));
    live_mode_set(true);assert(!g_fullscreen&&atomic_load(&g_session->scale));
    bili_live_play_platform(LIVE_CC,"6");
    assert(strcmp(g_requested_room,"6")==0);assert(g_requested_platform==LIVE_CC);
    assert(strcmp(g_session->room,"12345")==0);
    assert(atomic_load(&g_session->cancel));
    tick(g_timer);
    assert(strstr(lv_label_get_text(g_status),"6"));
    if (mode==1) stop_clicked(NULL);
    if (mode==2) { bili_live_stop();lv_obj_delete(page); }
    if (mode) assert(!g_requested_room[0]);
    atomic_store(&hold_http,false);
    for (int j=0;j<1000;j++) {
      pump();
      if (mode==0 && g_session && strcmp(g_session->room,"6")==0 && !atomic_load(&g_session->running)) break;
      if (mode==1 && g_session && g_session->joined) break;
      if (mode==2 && !g_session) break;
    }
    if (mode==0) assert(g_session && strcmp(g_session->room,"6")==0 && g_session->platform==LIVE_CC);
    if (mode==1) assert(g_session && strcmp(g_session->room,"12345")==0 && g_session->joined);
    if (mode==2) assert(!g_session && !g_timer);
    if (mode!=2) { bili_live_stop();lv_obj_delete(page); }
    for(int j=0;j<1000 && g_session;j++) pump();
    assert(!g_session);
    page=lv_obj_create(screen);lv_obj_set_pos(page,0,64);lv_obj_set_size(page,1024,536);
    bili_live_show(page,font);
  }
  /* Start/stop/delete repeatedly; transport is deterministic and never real. */
  for (int i=0;i<20;i++) {
    bili_live_play_room("12345");
    for (int j=0;j<100 && atomic_load(&g_session->running);j++) pump();
    assert(!atomic_load(&g_session->running));
    assert(frame(g_session,planes,strides,2,2)==0);
    tick(g_timer);
    const uint16_t *displayed=g_session->shown;
    assert(g_dsc.data==(const uint8_t *)displayed);
    assert(displayed[180*WIDTH+320]==0xffff);
    memset(Y,16,sizeof(Y));
    assert(frame(g_session,planes,strides,2,2)==0);
    assert(displayed[180*WIDTH+320]==0xffff); /* producer cannot mutate display */
    tick(g_timer);
    assert(g_session->shown[180*WIDTH+320]==0);
    assert(g_dsc.data==(const uint8_t *)g_session->shown);
    memset(Y,235,sizeof(Y));
    live_mode_set(false);assert(frame(g_session,planes,strides,2,2)==0);tick(g_timer);assert(g_dsc.header.w==2&&g_dsc.header.h==2&&g_session->shown[0]==0xffff);
    assert(lv_image_get_scale_x(g_image)==LV_SCALE_NONE);
    stop_clicked(NULL);bili_live_stop();lv_obj_delete(page);
    assert(!g_fullscreen&&lv_obj_get_child_count(lv_layer_top())==0);
    for(int j=0;j<100 && g_session;j++) pump();
    assert(!g_session && !g_timer);
    page=lv_obj_create(screen);lv_obj_set_pos(page,0,64);lv_obj_set_size(page,1024,536);
    bili_live_show(page,font);lv_textarea_set_text(g_room,"12345");
  }
  bili_live_stop();lv_obj_delete(page);pump();
  lv_tiny_ttf_destroy(font);lv_display_delete(display);lv_deinit();free(fontdata);
  puts("PASS: I420/RGB565 color and letterbox/cancel; LVGL 1024x600 render, room/keyboard bounds, room replacement, stop/exit wins, 20 start/stop/delete cycles (simulated transport)");
  return 0;
}
