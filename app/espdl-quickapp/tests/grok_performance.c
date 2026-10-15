/* SPDX-License-Identifier: Apache-2.0 */
/* Offline QuickJS/LVGL benchmark. No serial, network, board or flash access. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qpk_runtime.h"
#include "glass_icons.h"
extern int qpk_review_eval(const char *);
extern size_t qpk_review_memory_usage(void);
unsigned long long grok_test_rect_calls;
static uint8_t framebuffer[1024*600*2],draw_buffer[1024*120*2],reference[1024*600*2];
static const lv_font_t *fonts[3];
static const lv_font_t *font(int size){return fonts[size>=28?2:size>=20?1:0];}
static void message(const char *text){fprintf(stderr,"message: %s\n",text);}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels){
  for(int y=area->y1;y<=area->y2;y++){
    memcpy(framebuffer+(y*1024+area->x1)*2,pixels,(area->x2-area->x1+1)*2);
    pixels+=(area->x2-area->x1+1)*2;
  }
  lv_display_flush_ready(display);
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1000000.0;}
static char *read_file(const char *path,size_t *size){
  FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
  char *data=calloc(1,*size+1);assert(data);assert(fread(data,1,*size,f)==*size);fclose(f);return data;
}
static void eval(const char *script){assert(qpk_review_eval(script)==0);}
static void capture(const char *out,const char *key){
  char path[1024];snprintf(path,sizeof(path),"%s/%s.rgb565",out,key);FILE *f=fopen(path,"wb");assert(f);
  assert(fwrite(framebuffer,1,sizeof(framebuffer),f)==sizeof(framebuffer));fclose(f);
}
int main(int argc,char **argv){
  assert(argc==5);bool lean=strstr(argv[4],"lean")!=NULL,optimized=!strncmp(argv[4],"after",5);size_t font_size,source_size;
  char *font_data=read_file(argv[1],&font_size),*source=read_file(argv[2],&source_size);
  const char *seed="var __seed=0x12345678;Math.random=function(){__seed=(Math.imul(__seed,1664525)+1013904223)>>>0;return __seed/4294967296;};\n";
  char *script=malloc(strlen(seed)+source_size+1);assert(script);strcpy(script,seed);strcat(script,source);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,flush);
  for(unsigned i=0;i<3;i++){int size=i==2?28:i==1?20:16;fonts[i]=lv_tiny_ttf_create_data(font_data,font_size,size);assert(fonts[i]);((lv_font_t *)fonts[i])->fallback=glass_icon_font(size);}
  lv_obj_t *screen=lv_screen_active(),*root=lv_obj_create(screen);
  lv_obj_remove_style_all(root);lv_obj_set_pos(root,1,63);lv_obj_set_size(root,1022,536);
  lv_obj_set_style_bg_color(root,lv_color_hex(0xf8f9fa),0);lv_obj_set_style_bg_opa(root,255,0);
  const char *names[]={"idle","thinking","spin","confetti"};
  for(unsigned scenario=0;scenario<4;scenario++){
    assert(qpk_runtime_launch(root,"Grok Ball","grok.ball","2.0","app.js",script,strlen(script),font,message,message,NULL)==0);
    eval(scenario==1?"VelaMood.setEmotion('30')":scenario==3?"VelaMood.setEmotion('33')":"VelaMood.setEmotion('02')");
    if(scenario==2)eval("VelaMood.engine.spin(1)");
    double js_ms=0,draw_ms=0,max_ms=0;unsigned long long rectangles=grok_test_rect_calls;
    for(unsigned frame=0;frame<60;frame++){
      double a=now();eval("__reviewTick()");double b=now();lv_obj_update_layout(screen);lv_refr_now(display);double c=now();
      js_ms+=b-a;draw_ms+=c-b;if(c-a>max_ms)max_ms=c-a;
      if(!lean&&(frame==0||frame==8||frame==24||frame==59)){char key[80];snprintf(key,sizeof(key),"%s-%03u",names[scenario],frame);capture(argv[3],key);}
    }
    printf("METRIC {\"scenario\":\"%s\",\"frames\":60,\"js_ms\":%.3f,\"draw_ms\":%.3f,\"max_frame_ms\":%.3f,\"rectangles\":%llu,\"js_heap_bytes\":%zu}\n",names[scenario],js_ms,draw_ms,max_ms,grok_test_rect_calls-rectangles,qpk_review_memory_usage());fflush(stdout);
  }
  if(lean)goto cleanup;
  const char *ids[]={"00","01","02","03","04","05","06","07","10","11","12","13","14","15","16","17","18","19","20","21","30","31","32","33","34","35","36","37","38","39","40","41"};
  for(unsigned i=0;i<32;i++){
    char command[128],key[40];snprintf(command,sizeof(command),"VelaMood.setEmotion('%s');for(var n=0;n<24;n++)__reviewTick();VelaMood.renderStatic()",ids[i]);eval(command);
    lv_obj_update_layout(screen);lv_refr_now(display);snprintf(key,sizeof(key),"state-%s",ids[i]);capture(argv[3],key);
    /* Deliberately redraw a non-segment-aligned small rectangle. Its result
     * must match the full frame, including radial colour and AA edges. */
    memcpy(reference,framebuffer,sizeof(reference));lv_area_t dirty={457,211,537,262};lv_obj_invalidate_area(screen,&dirty);lv_refr_now(display);
    assert(!memcmp(reference,framebuffer,sizeof(reference)));
  }
  eval("var __probe=ui.polygon([[20,20],[80,20],[20,80]],0xffffff,255,0,[1,2,21.5,20.5,0,0,1e-300,0,0xffffff,1,0]);var __narrow=ui.polygon([[90,20],[150,20],[90,80]],0xffffff,255,0,[2,3,90,20,150,20,0,0,0xffffff,0.5,0xffffff,0.5000000001,0]);");
  lv_obj_update_layout(screen);lv_refr_now(display);capture(argv[3],"precision-probe");
  unsigned long long painted=grok_test_rect_calls;
  eval("ui.polygonSet(__probe,[[20,20],[80,20],[20,80]],0xffffff,255,0,[1,2,21.5,20.5,0,0,1e-300,0,0xffffff,1,0]);");lv_refr_now(display);
  if(optimized)assert(grok_test_rect_calls==painted);
  eval("function mustReject(fn){var rejected=false;try{fn();}catch(e){rejected=true;}if(!rejected)throw Error('invalid polygon accepted');}mustReject(function(){ui.polygon([[0,0],[8193,0],[0,1]]);});mustReject(function(){ui.polygon([[0,0],[1,0]]);});mustReject(function(){ui.polygon([[0,0],[1,0],[0,1]],0,255,0,[1,2,0,0,0,0,0,0,0,1,0]);});mustReject(function(){ui.polygon([[0,0],[1,0],[0,1]],0,255,0,[1,2,0,0,0,0,1,0.8,0,0.2,0]);});var __victim=ui.polygon([[0,0],[1,0],[0,1]]);var __point={get 0(){ui.remove(__victim);return 0;},1:0};mustReject(function(){ui.polygonSet(__victim,[__point,[1,0],[0,1]]);});ui.remove(__probe);ui.remove(__narrow);");
  eval("var __pairs=[[20,20],[80,20],[20,80]],__flat=new Int32Array([20,20,80,20,20,80]),__packed=typeof ui.polygonBuffer==='function';var __bufferProbe=__packed?ui.polygonBuffer(__flat.buffer,0x123456):ui.polygon(__pairs,0x123456);var __colour={valueOf:function(){__testDetach(__flat.buffer);return 0x654321;}};if(__packed)ui.polygonSetBuffer(__bufferProbe,__flat.buffer,__colour);else ui.polygonSet(__bufferProbe,__pairs,__colour);");
  lv_obj_update_layout(screen);lv_refr_now(display);capture(argv[3],"buffer-probe");
  eval("ui.remove(__bufferProbe);if(__packed){mustReject(function(){ui.polygonBuffer(new ArrayBuffer(25));});mustReject(function(){ui.polygonBuffer(new ArrayBuffer(129*8));});mustReject(function(){ui.polygonBuffer(new Int32Array([0,0,8193,0,0,1]).buffer);});mustReject(function(){ui.polygonBuffer(new Int32Array([0,0,1,0,0,1]));});var __detached=new ArrayBuffer(24);__testDetach(__detached);mustReject(function(){ui.polygonBuffer(__detached);});var __v=ui.polygonBuffer(new Int32Array([0,0,1,0,0,1]).buffer),__replacement;var __deleteColour={valueOf:function(){ui.remove(__v);__replacement=ui.polygonBuffer(new Int32Array([0,0,1,0,0,1]).buffer);return 0;}};mustReject(function(){ui.polygonSetBuffer(__v,new Int32Array([0,0,1,0,0,1]).buffer,__deleteColour);});ui.remove(__replacement);}");
  eval("if(__packed){var __throwColour={valueOf:function(){throw Error('conversion failure');}};mustReject(function(){ui.polygonBuffer(new Int32Array([0,0,1,0,0,1]).buffer,__throwColour);});mustReject(function(){ui.polygon([[0,0],[1,0],[0,1]],__throwColour);});mustReject(function(){ui.polygonBuffer(new Int32Array([0,0,1,0,0,1]).buffer,0,__throwColour);});mustReject(function(){ui.polygonBuffer(new Int32Array([0,0,1,0,0,1]).buffer,0,255,__throwColour);});}");
  puts("NATIVE_GROK_OK 32 states; four animations; 32 partial-refresh comparisons");
  puts("NATIVE_POLYGON_GUARDS_OK bounds, gradients, tiny-radius, narrow-stops, reentrant removal, unchanged update");
  puts("NATIVE_BUFFER_GUARDS_OK buffer snapshot, bounds, type, length, detach, recycled generation");
cleanup:
  qpk_runtime_stop();lv_obj_delete(root);lv_display_delete(display);
  for(unsigned i=0;i<3;i++)lv_tiny_ttf_destroy((lv_font_t *)fonts[i]);lv_deinit();free(script);free(source);free(font_data);return 0;
}
