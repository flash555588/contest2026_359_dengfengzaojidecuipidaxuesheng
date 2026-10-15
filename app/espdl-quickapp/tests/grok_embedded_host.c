/* SPDX-License-Identifier: Apache-2.0 */
/* Real component VM, renderer and ownership. Services are deterministic mocks. */
#include "glass_grok.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct chat_activity_snapshot chat;
static struct glass_voice_snapshot voice;
void glass_chat_activity_get(struct chat_activity_snapshot *out){*out=chat;}
void glass_voice_get(struct glass_voice_snapshot *out){*out=voice;}
static uint8_t fb[1024*600*2];
static const lv_font_t *fonts[3];
static const lv_font_t *font(int size){return fonts[size>=28?2:size>=20?1:0];}
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *pixels){memcpy(fb,pixels,sizeof(fb));lv_display_flush_ready(d);}
static void ticks(unsigned count){for(unsigned i=0;i<count;i++){lv_tick_inc(50);lv_timer_handler();}}
static void state(struct glass_grok *ball,enum chat_phase phase,enum chat_activity activity,unsigned expected){chat.phase=phase;chat.activity=activity;ticks(7);assert(glass_grok_ready(ball));assert(glass_grok_current(ball)==expected);}
static void capture(lv_display_t *d,const char *out,const char *name){lv_refr_now(d);char path[1024];snprintf(path,sizeof(path),"%s/%s.rgb565",out,name);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(fb,1,sizeof(fb),f)==sizeof(fb));fclose(f);}
static void registry(struct glass_grok *ball,lv_display_t *d){
  const unsigned ids[]={0,1,2,3,4,5,6,7,10,11,12,13,14,15,16,17,18,19,20,21,30,31,32,33,34,35,36,37,38,39,40,41};
  for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++){
    char id[4];snprintf(id,sizeof(id),"%02u",ids[i]);
    assert(glass_grok_test_expression(ball,id)==ids[i]);lv_refr_now(d);
    snprintf(id,sizeof(id),"%u",ids[i]);assert(glass_grok_test_expression(ball,id)==ids[i]);
  }
  const char *invalid[]={"08","09","99","-1","2.5","","002","__proto__"};
  for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)assert(glass_grok_test_expression(ball,invalid[i])==2);
  assert(glass_grok_test_expression(ball,"02")==2);
}
int main(int argc,char **argv){
  assert(argc==3);FILE *file=fopen(argv[1],"rb");assert(file);fseek(file,0,SEEK_END);size_t size=ftell(file);rewind(file);void *data=malloc(size);assert(data&&fread(data,1,size,file)==size);fclose(file);
  lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,fb,NULL,sizeof(fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
  for(int i=0;i<3;i++)fonts[i]=lv_tiny_ttf_create_data(data,size,i==2?28:i==1?20:16);
  lv_obj_t *screen=lv_screen_active(),*card=lv_obj_create(screen);lv_obj_remove_style_all(card);lv_obj_set_pos(card,108,374);lv_obj_set_size(card,285,178);lv_obj_set_style_bg_opa(card,255,0);lv_obj_set_style_bg_color(card,lv_color_hex(0xe9e2f2),0);
  chat.phase=CHAT_IDLE;chat.state=CHAT_DONE;struct glass_grok *ball=glass_grok_create(card,285,font);assert(ball);ticks(10);assert(glass_grok_ready(ball));assert(glass_grok_current(ball)==2);
  registry(ball,d);
  capture(d,argv[2],"idle");chat.turn_id=1;chat.state=CHAT_WAITING;state(ball,CHAT_QUEUED,CHAT_ACTIVITY_CONNECT,31);state(ball,CHAT_REQUESTING,CHAT_ACTIVITY_CONNECT,36);state(ball,CHAT_REQUESTING,CHAT_ACTIVITY_THINK,30);capture(d,argv[2],"thinking");
  state(ball,CHAT_REQUESTING,CHAT_ACTIVITY_REPLY,39);state(ball,CHAT_REQUESTING,CHAT_ACTIVITY_TOOL,32);state(ball,CHAT_REQUESTING,CHAT_ACTIVITY_THINK,30);state(ball,CHAT_REQUESTING,CHAT_ACTIVITY_REPLY,39);
  chat.state=CHAT_DONE;state(ball,CHAT_IDLE,CHAT_ACTIVITY_REPLY,33);capture(d,argv[2],"done");ticks(60);assert(glass_grok_current(ball)==2);ticks(20);assert(glass_grok_current(ball)==2);
  lv_obj_delete(card);card=lv_obj_create(screen);lv_obj_remove_style_all(card);lv_obj_set_size(card,180,144);ball=glass_grok_create(card,180,font);ticks(10);assert(glass_grok_ready(ball));assert(glass_grok_current(ball)==2);
  chat.turn_id=2;chat.state=CHAT_WAITING;state(ball,CHAT_REQUESTING,CHAT_ACTIVITY_THINK,30);chat.state=CHAT_FAILED;state(ball,CHAT_IDLE,CHAT_ACTIVITY_THINK,34);chat.state=CHAT_CANCELLED;state(ball,CHAT_IDLE,CHAT_ACTIVITY_THINK,41);
  chat.turn_id=0;voice.active=true;voice.phase=GLASS_VOICE_CONNECTING;state(ball,CHAT_IDLE,CHAT_ACTIVITY_THINK,36);voice.phase=GLASS_VOICE_LISTENING;ticks(7);assert(glass_grok_current(ball)==35);voice.phase=GLASS_VOICE_FINISHING;ticks(7);assert(glass_grok_current(ball)==32);voice.active=false;ticks(7);assert(glass_grok_current(ball)==2);
  state(ball,CHAT_STOPPING,CHAT_ACTIVITY_THINK,41);state(ball,CHAT_CLEARING,CHAT_ACTIVITY_THINK,36);
  state(ball,CHAT_REQUESTING,CHAT_ACTIVITY_THINK,30);lv_obj_add_flag(card,LV_OBJ_FLAG_HIDDEN);chat.activity=CHAT_ACTIVITY_REPLY;ticks(20);assert(glass_grok_current(ball)==30);lv_obj_remove_flag(card,LV_OBJ_FLAG_HIDDEN);ticks(7);assert(glass_grok_current(ball)==39);
  lv_obj_t *overlay=lv_obj_create(screen);glass_grok_set_obscured_by(ball,overlay);chat.activity=CHAT_ACTIVITY_THINK;ticks(20);assert(glass_grok_current(ball)==39);lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);ticks(7);assert(glass_grok_current(ball)==30);
  glass_grok_set_reduced_motion(ball,true);ticks(10);capture(d,argv[2],"static");uint8_t *reference=malloc(sizeof(fb));assert(reference);memcpy(reference,fb,sizeof(fb));ticks(30);lv_refr_now(d);assert(!memcmp(reference,fb,sizeof(fb)));free(reference);
  chat.activity=CHAT_ACTIVITY_REPLY;ticks(7);assert(glass_grok_current(ball)==39);
  /* Independent contexts can coexist; deleting one cannot stop the other. */
  lv_obj_t *other=lv_obj_create(screen);struct glass_grok *second=glass_grok_create_avatar(other,font);ticks(10);assert(glass_grok_ready(second));assert(lv_obj_get_width(lv_obj_get_child(other,0))==64&&lv_obj_get_height(lv_obj_get_child(other,0))==64);lv_obj_delete(card);ticks(10);assert(glass_grok_ready(second));lv_obj_delete(other);lv_obj_delete(overlay);
  other=lv_obj_create(screen);second=glass_grok_create_avatar(other,font);ticks(10);registry(second,d);lv_obj_delete(other);
  other=lv_obj_create(screen);second=glass_grok_create_pixel(other,285,false,font);ticks(10);registry(second,d);lv_obj_delete(other);
  for(unsigned cycle=0;cycle<12;cycle++){card=lv_obj_create(screen);ball=glass_grok_create(card,285,font);if(cycle%2)ticks(8);lv_obj_delete(card);ticks(2);}
  lv_obj_delete(screen);for(int i=0;i<3;i++)lv_tiny_ttf_destroy(fonts[i]);lv_display_delete(d);lv_deinit();free(data);
  puts("GROK_EMBEDDED_NATIVE_OK: all 32 original IDs rendered in black/ivory pixel and original mini; padded/unpadded IDs; invalid-ID fallback; real QuickJS/LVGL; typed stream transitions; completion once; voice; hidden pause; reduced motion; independent contexts; repeated teardown");return 0;
}
