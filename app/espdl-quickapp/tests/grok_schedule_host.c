/* SPDX-License-Identifier: Apache-2.0 */
/* Real LVGL timer scheduling, with deterministic reader completion between
 * virtual milliseconds. No production test-step hooks or device access. */
#include <unistd.h>
static unsigned seeks, callbacks;
static off_t counted_seek(int fd, off_t offset, int whence)
{
  seeks++;
  return lseek(fd, offset, whence);
}
#define lseek counted_seek
#include "glass_grok_player.inc"
#undef lseek
#include <assert.h>
#include <inttypes.h>
static struct chat_activity_snapshot chat;
static struct glass_voice_snapshot voice;
void glass_chat_activity_get(struct chat_activity_snapshot *out){*out=chat;}
void glass_voice_get(struct glass_voice_snapshot *out){*out=voice;}
static unsigned char framebuffer[300*180*2];
static unsigned frames;
static FILE *trace;
static const lv_font_t *font(int size){(void)size;return LV_FONT_DEFAULT;}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data)
{(void)a;(void)data;lv_display_flush_ready(d);}
static uint64_t hash(const unsigned char *data,size_t size)
{
  uint64_t value=UINT64_C(14695981039346656037);
  while(size--){value^=*data++;value*=UINT64_C(1099511628211);}
  return value;
}
static void counted_tick(lv_timer_t *timer){callbacks++;grok_tick(timer);}
static void settle_reader(struct glass_grok *b)
{
  for(unsigned i=0;i<5000;i++){
    pthread_mutex_lock(&b->lock);bool busy=b->busy;pthread_mutex_unlock(&b->lock);
    if(!busy)return;
    usleep(100);
  }
  assert(!"reader timed out");
}
static void step(struct glass_grok *b)
{
  unsigned before=b->displayed;
  lv_tick_inc(1);lv_timer_handler();settle_reader(b);
  if(b->displayed!=before){
    assert(!b->error);lv_refr_now(NULL);frames++;
    fprintf(trace,"%u %u %u %u %016" PRIx64 " %016" PRIx64 "\n",
            b->side,b->current,b->action,b->result_frame,
            hash(b->pixels[b->front],b->bytes),hash(framebuffer,sizeof(framebuffer)));
  }
}
static void pump(struct glass_grok *b,unsigned ms){while(ms--)step(b);}
static void next_frames(struct glass_grok *b,unsigned count)
{
  unsigned target=b->displayed+count;
  for(unsigned i=0;i<2000+count*150&&b->displayed<target;i++)step(b);
  assert(b->displayed==target);
}
static struct glass_grok *create(lv_obj_t *root,unsigned style)
{
  struct glass_grok *b=style==0?glass_grok_create_avatar(root,font):
    style<3?glass_grok_create_pixel(root,285,style==2,font):
    glass_grok_create_sprite(root,0,0,170,170,120,style==4,font);
  assert(b);lv_timer_set_cb(b->timer,counted_tick);return b;
}
int main(int argc,char **argv)
{
  assert(argc==2);trace=fopen(argv[1],"w");assert(trace);
  lv_init();lv_display_t *d=lv_display_create(300,180);
  lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d,framebuffer,NULL,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d,flush);
  lv_obj_t *root=lv_screen_active();lv_obj_remove_style_all(root);
  /* Stable animation: exact 100 ms cadence, same frames, fewer callbacks/seeks. */
  struct glass_grok *b=create(root,3);next_frames(b,1);
  unsigned start_callbacks=callbacks,start_seeks=seeks,before=b->displayed;
  uint32_t started=lv_tick_get(),last=started;
  for(unsigned i=0;i<10000;i++){
    unsigned old=b->displayed;step(b);
    if(b->displayed!=old){assert(lv_tick_get()-last==100);last=lv_tick_get();}
  }
  printf("METRIC {\"duration_ms\":10000,\"frames\":%u,\"callbacks\":%u,\"seeks\":%u}\n",
         b->displayed-before,callbacks-start_callbacks,seeks-start_seeks);
  lv_obj_t *overlay=lv_obj_create(root);glass_grok_set_obscured_by(b,overlay);
  pump(b,50);before=b->displayed;start_callbacks=callbacks;pump(b,1000);
  assert(b->displayed==before);
  printf("HIDDEN_METRIC {\"duration_ms\":1000,\"callbacks\":%u}\n",callbacks-start_callbacks);
  lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);next_frames(b,1);
  glass_grok_set_obscured_by(b,NULL);lv_obj_delete(overlay);
  lv_obj_delete(b->root);
  /* Compare all palettes and states byte-for-byte at multiple animation phases. */
  for(unsigned style=0;style<5;style++){
    b=create(root,style);next_frames(b,1);
    for(unsigned i=0;i<32;i++){glass_grok_select(b,ids[i]);next_frames(b,3);}
    if(style>=3){
      for(unsigned i=42;i<=51;i++){glass_grok_action(b,i);next_frames(b,i==42?31:3);}
      glass_grok_action(b,0);next_frames(b,1);
      for(int x=-1000;x<=1000;x+=500){assert(glass_grok_gaze(b,x,-x));next_frames(b,2);}
    }
    glass_grok_set_reduced_motion(b,true);pump(b,200);
    before=b->displayed;pump(b,500);assert(b->displayed==before);
    glass_grok_select(b,39);next_frames(b,1);
    before=b->displayed;pump(b,500);assert(b->displayed==before);
    glass_grok_set_reduced_motion(b,false);next_frames(b,1);
    glass_grok_set_active(b,false);before=b->displayed;pump(b,300);assert(b->displayed==before);
    glass_grok_set_active(b,true);next_frames(b,1);
    lv_obj_add_flag(b->root,LV_OBJ_FLAG_HIDDEN);before=b->displayed;pump(b,300);assert(b->displayed==before);
    lv_obj_remove_flag(b->root,LV_OBJ_FLAG_HIDDEN);next_frames(b,1);
    lv_obj_delete(b->root);
  }
  /* Invalidate a ready future frame mid-wait. New controls must wake <=20 ms,
   * including a tick counter rollover and a change of chat ownership. */
  b=create(root,3);next_frames(b,1);pump(b,27);
  glass_grok_select(b,30);started=lv_tick_get();
  while(!b->ready)step(b);assert(lv_tick_get()-started<=20);
  next_frames(b,1);assert(b->current==30);
  lv_tick_inc(UINT32_MAX-lv_tick_get()-10);pump(b,200);next_frames(b,2);
  glass_grok_follow_chat(b,true);chat.turn_id=1;chat.phase=CHAT_REQUESTING;
  chat.state=CHAT_WAITING;chat.activity=CHAT_ACTIVITY_REPLY;
  started=lv_tick_get();while(b->current!=39){step(b);assert(lv_tick_get()-started<=260);}
  next_frames(b,1);lv_obj_delete(b->root);
  fclose(trace);lv_obj_delete(root);lv_display_delete(d);lv_deinit();
  printf("GROK_SCHEDULE_OK frames=%u; 100 ms cadence; controls; rollover; hidden/reduced; all palettes/states\n",frames);
  return 0;
}
