/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only: the pinned SVG engine never executes in firmware playback. */
#include "offline_grok.c"
#include <assert.h>
void glass_chat_activity_get(struct chat_activity_snapshot *out){memset(out,0,sizeof(*out));}
void glass_voice_get(struct glass_voice_snapshot *out){memset(out,0,sizeof(*out));}
static unsigned char pixels[128*128*4];
static const lv_font_t *font(int size){(void)size;return &lv_font_montserrat_14;}
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *data)
{(void)area;(void)data;lv_display_flush_ready(d);}
int main(int argc,char **argv)
{
  assert(argc==3);unsigned variant=atoi(argv[2]),side=variant==2?64:128;
  const unsigned ids[]={0,1,2,3,4,5,6,7,10,11,12,13,14,15,16,17,18,19,20,21,30,31,32,33,34,35,36,37,38,39,40,41};
  lv_init();lv_display_t *d=lv_display_create(side,side);
  lv_display_set_color_format(d,LV_COLOR_FORMAT_ARGB8888);
  lv_display_set_buffers(d,pixels,NULL,side*side*4,LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d,flush);
  lv_obj_t *screen=lv_screen_active();lv_obj_remove_style_all(screen);
  lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_opa(screen,0,0);
  for(unsigned i=0;i<(variant>=3?42:32);i++){
    unsigned emotion=i<32?ids[i]:i+10;
    struct glass_grok *b=grok_create(screen,variant==2?64:285,variant==2?0:variant==1?4:3,font);assert(b);
    lv_timer_pause(b->timer);b->width=b->height=side;b->center_x=b->center_y=side/2;
    /* The standalone app retains the upstream camera, unlike the home card. */
    if(variant>=3)b->home_style=variant==4?6:5;
    lv_obj_set_size(b->root,side,side);assert(grok_init(b));b->initialized=true;
    char id[4];snprintf(id,sizeof(id),"%02u",i<32?emotion:2);assert(glass_grok_test_expression(b,id)==(i<32?emotion:2));
    if(i>=32){char action[128];
      if(i==32)strcpy(action,"GrokEmbedded.ball().spin(1)");
      else {int gaze=i-33;snprintf(action,sizeof(action),"GrokEmbedded.ball().setGaze(%d,%d)",gaze%3-1,gaze/3-1);}
      assert(grok_result(b,JS_Eval(b->context,action,strlen(action),"offline-action",JS_EVAL_TYPE_GLOBAL)));
    }
    char path[1024];snprintf(path,sizeof(path),"%s/v%u-%02u.bgra",argv[1],variant,emotion);
    FILE *f=fopen(path,"wb");assert(f);
    for(unsigned frame=0;frame<80;frame++){
      for(unsigned tick=0;tick<2;tick++){
        JSValue on=JS_NewBool(b->context,true);assert(grok_call(b,b->step,1,&on));JS_FreeValue(b->context,on);
      }
      lv_refr_now(d);
      uint16_t rgb[128*128];uint8_t alpha[128*128];
      for(unsigned p=0;p<side*side;p++){
        rgb[p]=((pixels[p*4+2]>>3)<<11)|((pixels[p*4+1]>>2)<<5)|(pixels[p*4]>>3);
        alpha[p]=pixels[p*4+3];
      }
      assert(fwrite(rgb,2,side*side,f)==side*side);
      assert(fwrite(alpha,1,side*side,f)==side*side);
    }
    fclose(f);lv_obj_delete(b->root);
    printf("ASSET v%u id%02u 80 frames\n",variant,emotion);fflush(stdout);
  }
  lv_obj_delete(screen);lv_display_delete(d);lv_deinit();return 0;
}
