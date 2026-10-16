/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_motion.h"
#include <assert.h>
#include <stdio.h>
static uint8_t pixels[320*180*2],baseline[sizeof(pixels)];
static unsigned clicks,completed;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p)
{(void)a;(void)p;lv_display_flush_ready(d);}
static void pump(unsigned ms)
{for(unsigned i=0;i<ms;i+=20){lv_tick_inc(20);lv_timer_handler();}}
static void clicked(lv_event_t *e){LV_UNUSED(e);clicks++;}
static void done(lv_obj_t *obj){completed++;lv_obj_delete(obj);}
static unsigned jobs(void)
{unsigned n=0;for(unsigned i=0;i<GLASS_MOTION_SLOTS;i++)n+=g_motion_jobs[i].object!=NULL;return n;}
static void snapshot(lv_display_t *d){lv_obj_invalidate(lv_screen_active());lv_refr_now(d);}
int main(void)
{
  lv_init();lv_display_t *d=lv_display_create(320,180);
  lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d,flush);
  lv_obj_t *root=lv_screen_active(),*button=lv_button_create(root);
  lv_obj_remove_style_all(button);lv_obj_set_pos(button,30,40);lv_obj_set_size(button,140,48);
  lv_obj_set_style_bg_color(button,lv_color_hex(0xebe6f4),0);
  lv_obj_set_style_bg_opa(button,255,0);lv_obj_set_style_translate_y(button,3,0);
  glass_motion_button(button);glass_motion_button(button);
  lv_obj_add_event_cb(button,clicked,LV_EVENT_CLICKED,NULL);
  snapshot(d);memcpy(baseline,pixels,sizeof(pixels));
  lv_obj_send_event(button,LV_EVENT_PRESSED,NULL);pump(100);
  assert(jobs()==1&&lv_timer_get_paused(g_motion_timer));
  assert(lv_obj_get_style_translate_y(button,0)==5);
  lv_obj_send_event(button,LV_EVENT_RELEASED,NULL);
  lv_obj_send_event(button,LV_EVENT_CLICKED,NULL);assert(clicks==1);
  pump(200);assert(!jobs()&&lv_timer_get_paused(g_motion_timer));
  snapshot(d);assert(!memcmp(pixels,baseline,sizeof(pixels)));
  for(unsigned i=0;i<100;i++){
    lv_obj_send_event(button,LV_EVENT_PRESSED,NULL);pump(20);
    lv_obj_send_event(button,LV_EVENT_PRESS_LOST,NULL);pump(20);
  }
  pump(200);assert(!jobs());snapshot(d);assert(!memcmp(pixels,baseline,sizeof(pixels)));
  lv_obj_send_event(button,LV_EVENT_PRESSED,NULL);pump(40);
  lv_obj_set_style_bg_color(button,lv_color_hex(0x339966),0);
  pump(20);lv_obj_send_event(button,LV_EVENT_RELEASED,NULL);pump(200);
  assert(lv_color_to_u32(lv_obj_get_style_bg_color(button,0))==lv_color_to_u32(lv_color_hex(0x339966)));
  lv_obj_send_event(button,LV_EVENT_PRESSED,NULL);pump(40);
  glass_motion_set_reduced(true);assert(!jobs());
  assert(lv_obj_get_style_translate_y(button,0)==3);
  glass_motion_enter(button,true,0);assert(!jobs());
  glass_motion_set_reduced(false);
  lv_obj_t *mark=lv_obj_create(root);lv_obj_remove_flag(mark,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(mark,0,0);lv_obj_set_size(mark,20,6);lv_obj_update_layout(mark);
  assert(glass_motion_geometry(mark,120,40,20,6,1,true));
  assert(glass_motion_geometry(mark,120,40,80,12,2,true));pump(60);
  struct glass_motion_job *moving=NULL;
  for(unsigned i=0;i<GLASS_MOTION_SLOTS;i++)if(g_motion_jobs[i].object==mark)moving=&g_motion_jobs[i];
  assert(moving);int64_t position=moving->position[0],velocity=moving->velocity[0];
  assert(position>0&&position<120*GLASS_MOTION_FIXED&&velocity>0);
  assert(glass_motion_geometry(mark,-40,20,80,12,1,true));
  assert(moving->start[0]==position&&moving->initial_velocity[0]==velocity);
  assert(moving->position[0]==position); /* No positional or velocity reset. */
  uint32_t started=moving->started;pump(20);
  assert(glass_motion_geometry(mark,-40,20,80,12,1,true));assert(moving->started==started);
  pump(260);lv_obj_update_layout(mark);assert(!jobs());
  assert(lv_obj_get_x(mark)==-40&&lv_obj_get_y(mark)==20&&lv_obj_get_width(mark)==80);
  assert(glass_motion_geometry(mark,60,60,80,12,1,true));pump(20);
  glass_motion_set_reduced(true);lv_obj_update_layout(mark);
  assert(lv_obj_get_x(mark)==60&&!jobs());glass_motion_set_reduced(false);
  assert(!glass_motion_geometry(button,10,10,20,20,3,true)); /* Hit targets stay direct. */
  assert(glass_motion_geometry(mark,80,80,80,12,1,true));pump(20);
  assert(!glass_motion_geometry(mark,10,10,80,12,1,false));lv_obj_set_pos(mark,10,10);
  assert(!jobs());
  lv_obj_set_size(mark,6,6);lv_obj_set_style_bg_color(mark,lv_color_hex(0x202020),0);
  assert(glass_motion_geometry(mark,3,10,20,6,3,true));
  assert(glass_motion_color(mark,0xe0e0e0,true));assert(jobs()==2);pump(70);
  uint32_t middle=lv_color_to_u32(lv_obj_get_style_bg_color(mark,0));
  assert(middle!=lv_color_to_u32(lv_color_hex(0x202020))&&middle!=lv_color_to_u32(lv_color_hex(0xe0e0e0)));
  assert(glass_motion_geometry(mark,10,10,6,6,3,true));
  assert(glass_motion_color(mark,0x202020,true));assert(jobs()==2);
  assert(lv_color_to_u32(lv_obj_get_style_bg_color(mark,0))==middle);
  pump(260);assert(!jobs());lv_obj_update_layout(mark);
  assert(lv_obj_get_width(mark)==6&&lv_obj_get_height(mark)==6);
  assert(lv_color_to_u32(lv_obj_get_style_bg_color(mark,0))==lv_color_to_u32(lv_color_hex(0x202020)));
  assert(glass_motion_color(mark,0xe0e0e0,true));pump(30);
  glass_motion_set_reduced(true);assert(!jobs());
  assert(lv_color_to_u32(lv_obj_get_style_bg_color(mark,0))==lv_color_to_u32(lv_color_hex(0xe0e0e0)));
  glass_motion_set_reduced(false);lv_obj_delete(mark);
  /* Native range interpolation: endpoint colors, direct dragging, and reversal. */
  lv_obj_t *rail=lv_obj_create(root);lv_obj_remove_flag(rail,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_main_stop(rail,0,0);
  assert(glass_motion_range(rail,255,0xffa64e,0x303640,true));pump(70);
  unsigned stop=lv_obj_get_style_bg_main_stop(rail,0);assert(stop>0&&stop<255);
  assert(glass_motion_range(rail,0,0xffa64e,0x303640,true));
  assert(lv_obj_get_style_bg_main_stop(rail,0)==stop);pump(280);assert(!jobs());
  assert(lv_obj_get_style_bg_main_stop(rail,0)==0);
  assert(lv_color_to_u32(lv_obj_get_style_bg_color(rail,0))==lv_color_to_u32(lv_color_hex(0x303640)));
  assert(glass_motion_range(rail,255,0xffa64e,0x303640,true));pump(280);
  assert(lv_color_to_u32(lv_obj_get_style_bg_grad_color(rail,0))==lv_color_to_u32(lv_color_hex(0xffa64e)));
  assert(glass_motion_range(rail,0,0xffa64e,0x303640,true));pump(20);
  assert(!glass_motion_range(rail,128,0xffa64e,0x303640,false));assert(!jobs());lv_obj_delete(rail);
  /* One scalar drives the track and handle, including interruption/deletion. */
  rail=lv_obj_create(root);lv_obj_remove_flag(rail,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(rail,20,30);lv_obj_set_size(rail,234,48);lv_obj_set_style_bg_main_stop(rail,0,0);
  lv_obj_t *thumb=lv_obj_create(root);lv_obj_remove_flag(thumb,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_size(thumb,6,28);
  assert(glass_motion_range_linked(rail,thumb,255,0xffa64e,0x303640,true));assert(jobs()==1);
  for(unsigned frame=0;frame<4;frame++){
    pump(33);lv_obj_update_layout(thumb);int s=lv_obj_get_style_bg_main_stop(rail,0);
    assert(lv_obj_get_x(thumb)==32+(204*s+127)/255);assert(lv_obj_get_y(thumb)==40);
  }
  int before=lv_obj_get_style_x(thumb,0);
  assert(glass_motion_range_linked(rail,thumb,0,0xffa64e,0x303640,true));assert(lv_obj_get_style_x(thumb,0)==before);
  pump(33);lv_obj_delete(thumb);pump(280);assert(!jobs());
  thumb=lv_obj_create(root);lv_obj_set_size(thumb,6,28);
  glass_motion_range_linked(rail,thumb,255,0xffa64e,0x303640,true);glass_motion_set_reduced(true);
  assert(!jobs());assert(lv_obj_get_style_x(thumb,0)==236);
  glass_motion_set_reduced(false);lv_obj_delete(rail);lv_obj_delete(thumb);
  /* A bounded batch shares one job; deletion and interruption restore every member. */
  lv_obj_t *members[GLASS_MOTION_BATCH_LIMIT];
  for(unsigned i=0;i<GLASS_MOTION_BATCH_LIMIT;i++){
    members[i]=lv_obj_create(root);lv_obj_remove_flag(members[i],LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_translate_y(members[i],3,0);
  }
  glass_motion_present(members,GLASS_MOTION_BATCH_LIMIT,12);assert(jobs()==1);
  assert(lv_obj_get_style_translate_y(members[100],0)==15);pump(70);
  lv_obj_delete(members[80]);members[80]=NULL;
  lv_obj_delete(members[0]);members[0]=NULL;assert(!jobs());
  for(unsigned i=1;i<GLASS_MOTION_BATCH_LIMIT;i++)if(members[i])assert(lv_obj_get_style_translate_y(members[i],0)==3);
  glass_motion_present_axis(members+1,70,12,true);assert(jobs()==1);
  assert(lv_obj_get_style_translate_x(members[1],0)==12);
  assert(lv_obj_get_style_translate_y(members[1],0)==3);pump(30);
  glass_motion_present(members+1,70,-12);
  assert(lv_obj_get_style_translate_x(members[1],0)==0);
  assert(lv_obj_get_style_translate_y(members[1],0)==-9);
  pump(30);glass_motion_set_reduced(true);
  assert(!jobs()&&!g_motion_member_count);
  for(unsigned i=1;i<GLASS_MOTION_BATCH_LIMIT;i++)if(members[i]){assert(lv_obj_get_style_translate_y(members[i],0)==3);lv_obj_delete(members[i]);}
  glass_motion_set_reduced(false);
  lv_obj_send_event(button,LV_EVENT_PRESSED,NULL);pump(20);
  lv_obj_add_state(button,LV_STATE_DISABLED);lv_obj_send_event(button,LV_EVENT_PRESS_LOST,NULL);
  pump(200);assert(!jobs());lv_obj_remove_state(button,LV_STATE_DISABLED);
  lv_obj_t *panel=lv_obj_create(root);glass_motion_panel(panel);lv_obj_delete(panel);pump(100);assert(!jobs());
  panel=lv_obj_create(root);glass_motion_panel(panel);lv_obj_remove_style_all(panel);pump(20);
  assert(jobs()==1&&lv_obj_get_style_translate_y(panel,0)>0);pump(260);
  assert(!jobs()&&lv_obj_get_style_translate_y(panel,0)==0);lv_obj_delete(panel);
  lv_obj_t *notice=lv_label_create(root);lv_label_set_text(notice,"Ready");
  lv_obj_set_style_bg_opa(notice,220,0);glass_motion_notice(notice);pump(220);
  assert(lv_obj_get_style_bg_opa(notice,0)==220&&!jobs());
  assert(glass_motion_dismiss(notice,done));pump(160);assert(completed==1&&!jobs());
  notice=lv_label_create(root);glass_motion_notice(notice);pump(20);lv_obj_delete(notice);pump(200);assert(!jobs());
  lv_obj_t *many[GLASS_MOTION_SLOTS];
  for(unsigned i=0;i<GLASS_MOTION_SLOTS;i++){many[i]=lv_obj_create(root);glass_motion_enter(many[i],false,0);}
  assert(jobs()==GLASS_MOTION_SLOTS);
  glass_motion_enter(button,false,0);assert(jobs()==GLASS_MOTION_SLOTS);
  glass_motion_set_reduced(true);assert(!jobs());
  for(unsigned i=0;i<GLASS_MOTION_SLOTS;i++){assert(lv_obj_get_style_translate_y(many[i],0)==0);lv_obj_delete(many[i]);}
  glass_motion_set_reduced(false);lv_tick_inc(UINT32_MAX-lv_tick_get()-10);
  glass_motion_enter(button,true,0);pump(260);assert(!jobs());
  lv_obj_send_event(button,LV_EVENT_PRESSED,NULL);pump(20);lv_obj_delete(button);pump(200);
  assert(!jobs()&&lv_timer_get_paused(g_motion_timer));
  lv_timer_delete(g_motion_timer);g_motion_timer=NULL;
  lv_obj_delete(root);lv_display_delete(d);lv_deinit();
  puts("INTERACTION_MOTION_OK: range endpoints/reversal/direct drag; 128-member batch deletion/reduced motion; C1 geometry retarget; combined move/resize; duplicate target deadline; direct hit targets; pixel restoration; semantic state update retained; 100 interrupted presses; immediate click; press lost/disabled; reduced motion; deferred deletion; toast teardown; bounded saturation; rollover; paused idle");
  return 0;
}
