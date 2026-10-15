/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_grok.h"
#include "qpk_limits.h"
#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GROK_WIDGETS 96
struct grok_widget { lv_obj_t *obj; void *extra; uint64_t generation; };
struct glass_grok {
  lv_obj_t *root,*overlay; lv_timer_t *timer; glass_grok_font_t font;
  JSRuntime *runtime; JSContext *context; JSValue step,emotion;
  struct grok_widget widgets[GROK_WIDGETS];
  uint64_t deadline; uint32_t turn_id,settled_at,polled_at;
  unsigned current,width,height,diameter,center_x,center_y,body_cell,detail_cell;
  bool initialized,failed,reduced,settled,celebrating;
  unsigned home_style;
};
extern const char glass_grok_js[];
extern const unsigned glass_grok_js_size;
extern const char glass_grok_driver_js[];
extern const unsigned glass_grok_driver_js_size;
extern const unsigned glass_grok_engine_offset,glass_grok_engine_size;
extern const char *ouo_get_app_js(unsigned int *len);
#ifdef GROK_HOST_TEST
static struct glass_grok *grok_test_instances[8];
#endif
static uint64_t grok_now(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static bool grok_current(JSContext *c,int h,uint64_t generation)
{ struct glass_grok *b=JS_GetContextOpaque(c);return h>0&&h<=GROK_WIDGETS&&b->widgets[h-1].obj&&b->widgets[h-1].generation==generation; }
#include "qpk_polygon_draw.inc"
#define QPK_POLYGON_CURRENT(c,h,g) grok_current(c,h,g)
#include "qpk_polygon_convert.inc"
#undef QPK_POLYGON_CURRENT

static int grok_slot(struct glass_grok *b)
{ for(int i=0;i<GROK_WIDGETS;i++)if(!b->widgets[i].obj)return i;return -1; }
static void grok_remove(struct grok_widget *w)
{ if(w->obj)lv_obj_delete(w->obj);free(w->extra);w->obj=NULL;w->extra=NULL;w->generation++; }
static int grok_int(JSContext *c,int argc,JSValueConst *argv,int index,int32_t *out)
{ if(index>=argc)return -1;return JS_ToInt32(c,out,argv[index]); }

enum { POLYGON, POLYGON_SET, TEXT, SET_TEXT, SET_POS, SET_COLOR,
       SET_OPACITY, SET_STYLE, SET_SIZE, HIDDEN, FRONT, REMOVE, NOW };
static JSValue grok_ui(JSContext *c,JSValueConst self,int argc,JSValueConst *argv,int op)
{
  (void)self;struct glass_grok *b=JS_GetContextOpaque(c);
  if(op==NOW)return JS_NewFloat64(c,grok_now());
  if(op==POLYGON){
    int slot=grok_slot(b);if(slot<0)return JS_ThrowRangeError(c,"Grok widget limit");
    struct qpk_polygon_s *p=calloc(1,sizeof(*p));if(!p)return JS_ThrowOutOfMemory(c);
    lv_obj_t *obj=lv_obj_create(b->root);if(!obj){free(p);return JS_ThrowOutOfMemory(c);}
    lv_obj_remove_style_all(obj);lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    JSValue result=qpk_polygon_update(c,obj,p,argc,argv,0,0,0,true);
    if(JS_IsException(result)){lv_obj_delete(obj);free(p);return result;}
    b->widgets[slot].obj=obj;b->widgets[slot].extra=p;b->widgets[slot].generation++;
    lv_obj_add_event_cb(obj,qpk_polygon_draw,LV_EVENT_DRAW_MAIN,p);return JS_NewInt32(c,slot+1);
  }
  if(op==TEXT){
    int32_t x,y,size;uint32_t color;
    if(argc<5||grok_int(c,argc,argv,1,&x)||grok_int(c,argc,argv,2,&y)||grok_int(c,argc,argv,3,&size)||JS_ToUint32(c,&color,argv[4]))return JS_EXCEPTION;
    const char *text=JS_ToCString(c,argv[0]);if(!text)return JS_EXCEPTION;
    int slot=grok_slot(b);if(slot<0){JS_FreeCString(c,text);return JS_ThrowRangeError(c,"Grok widget limit");}
    lv_obj_t *obj=lv_label_create(b->root);if(!obj){JS_FreeCString(c,text);return JS_ThrowOutOfMemory(c);}
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_label_set_text(obj,text);JS_FreeCString(c,text);
    lv_obj_set_pos(obj,x,y);lv_obj_set_style_text_font(obj,b->font(size),0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    b->widgets[slot].obj=obj;b->widgets[slot].generation++;return JS_NewInt32(c,slot+1);
  }
  int32_t h;if(grok_int(c,argc,argv,0,&h)||h<1||h>GROK_WIDGETS||!b->widgets[h-1].obj)return JS_ThrowRangeError(c,"invalid Grok widget");
  struct grok_widget *w=&b->widgets[h-1];uint64_t generation=w->generation;
  if(op==POLYGON_SET){if(!w->extra)return JS_ThrowTypeError(c,"not a contour");return qpk_polygon_update(c,w->obj,w->extra,argc,argv,1,h,generation,true);}
  if(op==REMOVE){grok_remove(w);return JS_UNDEFINED;}
  if(op==FRONT){lv_obj_move_foreground(w->obj);return JS_UNDEFINED;}
  if(op==HIDDEN){int hidden=argc>1?JS_ToBool(c,argv[1]):1;if(hidden<0)return JS_EXCEPTION;if(hidden)lv_obj_add_flag(w->obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(w->obj,LV_OBJ_FLAG_HIDDEN);return JS_UNDEFINED;}
  if(op==SET_TEXT){if(w->extra||argc<2)return JS_ThrowTypeError(c,"invalid Grok text");const char *text=JS_ToCString(c,argv[1]);if(!text)return JS_EXCEPTION;if(!grok_current(c,h,generation)){JS_FreeCString(c,text);return JS_ThrowRangeError(c,"Grok text removed");}lv_label_set_text(w->obj,text);JS_FreeCString(c,text);return JS_UNDEFINED;}
  int32_t x=0,y=0;uint32_t color=0;
  if(op==SET_STYLE){JSValue size=argc>1?JS_GetPropertyStr(c,argv[1],"fontSize"):JS_UNDEFINED;int ret=JS_ToInt32(c,&x,size);JS_FreeValue(c,size);if(ret)return JS_EXCEPTION;}
  else if(op==SET_COLOR){if(argc<2||JS_ToUint32(c,&color,argv[1]))return JS_EXCEPTION;}
  else {if(grok_int(c,argc,argv,1,&x))return JS_EXCEPTION;if((op==SET_POS||op==SET_SIZE)&&grok_int(c,argc,argv,2,&y))return JS_EXCEPTION;}
  if(!grok_current(c,h,generation))return JS_ThrowRangeError(c,"Grok widget removed");
  if(op==SET_POS)lv_obj_set_pos(w->obj,x,y);
  else if(op==SET_SIZE)lv_obj_set_size(w->obj,x,y);
  else if(op==SET_COLOR)lv_obj_set_style_text_color(w->obj,lv_color_hex(color),0);
  else if(op==SET_OPACITY)lv_obj_set_style_opa(w->obj,x<0?0:x>255?255:x,0);
  else if(op==SET_STYLE)lv_obj_set_style_text_font(w->obj,b->font(x<12?12:x>48?48:x),0);
  return JS_UNDEFINED;
}

static int grok_interrupt(JSRuntime *runtime,void *opaque)
{ (void)runtime;struct glass_grok *b=opaque;return b->deadline&&grok_now()>=b->deadline; }
static void grok_release_vm(struct glass_grok *b)
{
  if(b->context){JS_FreeValue(b->context,b->step);JS_FreeValue(b->context,b->emotion);JS_FreeContext(b->context);b->context=NULL;}
  if(b->runtime){JS_FreeRuntime(b->runtime);b->runtime=NULL;}
  b->step=b->emotion=JS_UNDEFINED;
}
static bool grok_result(struct glass_grok *b,JSValue result)
{
  bool ok=!JS_IsException(result);JS_FreeValue(b->context,result);
  if(!ok){JSValue ex=JS_GetException(b->context);const char *text=JS_ToCString(b->context,ex);fprintf(stderr,"[grok] embedded animation stopped: %s\n",text?text:"allocation or execution failure");JS_FreeCString(b->context,text);JS_FreeValue(b->context,ex);b->failed=true;}
  return ok;
}
static bool grok_call(struct glass_grok *b,JSValue function,int argc,JSValue *args)
{ b->deadline=grok_now()+QPK_JS_EVENT_TIMEOUT_MS;JSValue result=JS_Call(b->context,function,JS_UNDEFINED,argc,args);b->deadline=0;return grok_result(b,result); }
static bool grok_init(struct glass_grok *b)
{
  b->runtime=JS_NewRuntime();if(!b->runtime)return false;
  JS_SetMemoryLimit(b->runtime,2*1024*1024);
#ifdef __NuttX__
  JS_SetMaxStackSize(b->runtime,16*1024);
#else
  /* The 64-bit ASan host has much larger C parser frames than RV32 firmware.
   * Sanitizer lifecycle tests cannot establish the target stack headroom. */
  JS_SetMaxStackSize(b->runtime,256*1024);
#endif
  JS_SetInterruptHandler(b->runtime,grok_interrupt,b);
  b->context=JS_NewContext(b->runtime);if(!b->context)return false;
  JSContext *c=b->context;JS_SetContextOpaque(c,b);
  JSValue global=JS_GetGlobalObject(c),ui=JS_NewObject(c);
  const char *names[]={"polygonBuffer","polygonSetBuffer","text","setText","setPos","setColor","setOpacity","setStyle","setSize","setHidden","front","remove","nowMs"};
  for(int op=0;op<=NOW;op++)JS_SetPropertyStr(c,ui,names[op],JS_NewCFunctionMagic(c,grok_ui,names[op],0,JS_CFUNC_generic_magic,op));
  JS_SetPropertyStr(c,global,"ui",ui);JS_FreeValue(c,global);
  unsigned app_size=0;const char *app=ouo_get_app_js(&app_size);
  if(!app||glass_grok_engine_offset>app_size||glass_grok_engine_size>app_size-glass_grok_engine_offset)return false;
  /* JS_Eval requires a trailing NUL even when a source length is supplied.
   * The shared SDK sits inside OuO's full script; copy only during parsing. */
  char *engine=malloc((size_t)glass_grok_engine_size+1);if(!engine)return false;
  memcpy(engine,app+glass_grok_engine_offset,glass_grok_engine_size);engine[glass_grok_engine_size]=0;
  const char *scripts[]={glass_grok_js,engine,glass_grok_driver_js};
  const unsigned sizes[]={glass_grok_js_size,glass_grok_engine_size,glass_grok_driver_js_size};
  const char *filenames[]={"grok-adapter.js","grok-shared-engine.js","grok-embedded.js"};
  JSValue compiled[3]={JS_UNDEFINED,JS_UNDEFINED,JS_UNDEFINED};
  /* All source spans share one compile deadline and one execution deadline.
   * Only immutable source bytes are shared; VM state remains per component. */
  b->deadline=grok_now()+QPK_JS_COMPILE_TIMEOUT_MS;
  for(unsigned i=0;i<3;i++){
    compiled[i]=JS_Eval(c,scripts[i],sizes[i],filenames[i],JS_EVAL_TYPE_GLOBAL|JS_EVAL_FLAG_COMPILE_ONLY);
    if(JS_IsException(compiled[i])){
      JSValue result=compiled[i];compiled[i]=JS_UNDEFINED;b->deadline=0;
      free(engine);for(unsigned j=0;j<3;j++)JS_FreeValue(c,compiled[j]);return grok_result(b,result);
    }
  }
  free(engine);
  b->deadline=grok_now()+QPK_JS_EVENT_TIMEOUT_MS;
  for(unsigned i=0;i<3;i++){
    JSValue result=JS_EvalFunction(c,compiled[i]);compiled[i]=JS_UNDEFINED;
    if(JS_IsException(result)){
      b->deadline=0;for(unsigned j=0;j<3;j++)JS_FreeValue(c,compiled[j]);return grok_result(b,result);
    }
    JS_FreeValue(c,result);
  }
  b->deadline=0;
  global=JS_GetGlobalObject(c);JSValue embedded=JS_GetPropertyStr(c,global,"GrokEmbedded");JS_FreeValue(c,global);
  JSValue init=JS_GetPropertyStr(c,embedded,"init");b->step=JS_GetPropertyStr(c,embedded,"step");b->emotion=JS_GetPropertyStr(c,embedded,"emotion");JS_FreeValue(c,embedded);
  JSValue args[8]={JS_NewInt32(c,b->width),JS_NewInt32(c,b->height),JS_NewInt32(c,b->diameter),JS_NewInt32(c,b->center_x),JS_NewInt32(c,b->center_y),JS_NewInt32(c,b->body_cell),JS_NewInt32(c,b->detail_cell),JS_NewInt32(c,b->home_style)};
  bool ok=grok_call(b,init,8,args);for(unsigned i=0;i<8;i++)JS_FreeValue(c,args[i]);JS_FreeValue(c,init);return ok;
}
static void grok_tick(lv_timer_t *timer)
{
  struct glass_grok *b=lv_timer_get_user_data(timer);
  if(b->failed||!lv_obj_is_visible(b->root)||(b->overlay&&lv_obj_is_visible(b->overlay)))return;
  if(!b->initialized){if(!grok_init(b)){b->failed=true;grok_release_vm(b);for(int i=0;i<GROK_WIDGETS;i++)grok_remove(&b->widgets[i]);lv_obj_add_flag(b->root,LV_OBJ_FLAG_HIDDEN);return;}b->initialized=true;}
  uint32_t now=lv_tick_get();
  if(now-b->polled_at>=250||!b->polled_at){
    struct chat_activity_snapshot chat;struct glass_voice_snapshot voice;
    glass_chat_activity_get(&chat);glass_voice_get(&voice);unsigned emotion=glass_grok_emotion(&chat,&voice);
    /* Do not celebrate a stored history turn on reopening. Completion is a
     * transition, played once for a newly finished live request. */
    if(chat.turn_id!=b->turn_id){b->turn_id=chat.turn_id;b->settled=chat.phase==CHAT_IDLE;b->celebrating=false;}
    if(emotion==33){
      if(!b->settled){b->settled_at=now;b->settled=true;b->celebrating=true;}
      if(!b->celebrating||now-b->settled_at>=2500){emotion=2;b->celebrating=false;}
    }
    if(emotion!=b->current){JSValue id=JS_NewInt32(b->context,emotion);bool ok=grok_call(b,b->emotion,1,&id);JS_FreeValue(b->context,id);if(!ok)goto failed;b->current=emotion;}
    b->polled_at=now;
  }
  {JSValue animate=JS_NewBool(b->context,!b->reduced);bool ok=grok_call(b,b->step,1,&animate);JS_FreeValue(b->context,animate);if(ok)return;}
failed:
  grok_release_vm(b);for(int i=0;i<GROK_WIDGETS;i++)grok_remove(&b->widgets[i]);lv_obj_add_flag(b->root,LV_OBJ_FLAG_HIDDEN);lv_timer_pause(b->timer);
}
static void grok_deleted(lv_event_t *e)
{
  struct glass_grok *b=lv_event_get_user_data(e);if(b->timer)lv_timer_delete(b->timer);
#ifdef GROK_HOST_TEST
  for(unsigned i=0;i<8;i++)if(grok_test_instances[i]==b)grok_test_instances[i]=NULL;
#endif
  grok_release_vm(b);for(int i=0;i<GROK_WIDGETS;i++){free(b->widgets[i].extra);b->widgets[i].extra=NULL;}free(b);
}
static struct glass_grok *grok_create(lv_obj_t *parent,int width,unsigned style,glass_grok_font_t font)
{
  bool avatar=style==0;
  if(!parent||!font||(!avatar&&(width<160||width>285)))return NULL;
  struct glass_grok *b=calloc(1,sizeof(*b));if(!b)return NULL;b->step=b->emotion=JS_UNDEFINED;b->width=width;b->font=font;b->current=2;
  bool pixel=avatar||style>=3;
  b->height=avatar?64:89;b->diameter=avatar?48:96;b->center_x=avatar?32:76;b->center_y=avatar?32:65;b->body_cell=pixel?1:4;b->detail_cell=pixel?1:2;
  b->home_style=style;
  b->root=lv_obj_create(parent);if(!b->root){free(b);return NULL;}
  lv_obj_remove_style_all(b->root);lv_obj_set_pos(b->root,0,0);lv_obj_set_size(b->root,width,b->height);lv_obj_clear_flag(b->root,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(b->root,grok_deleted,LV_EVENT_DELETE,b);b->timer=lv_timer_create(grok_tick,50,b);
  if(!b->timer){lv_obj_delete(b->root);return NULL;}
#ifdef GROK_HOST_TEST
  for(unsigned i=0;i<8;i++)if(!grok_test_instances[i]){grok_test_instances[i]=b;break;}
#endif
  return b;
}
struct glass_grok *glass_grok_create(lv_obj_t *parent,int width,glass_grok_font_t font){return grok_create(parent,width,4,font);}
struct glass_grok *glass_grok_create_pixel(lv_obj_t *parent,int width,bool black,glass_grok_font_t font){return grok_create(parent,width,black?4:3,font);}
struct glass_grok *glass_grok_create_mosaic(lv_obj_t *parent,int width,bool black,glass_grok_font_t font){return grok_create(parent,width,black?2:1,font);}
struct glass_grok *glass_grok_create_avatar(lv_obj_t *parent,glass_grok_font_t font){return grok_create(parent,64,0,font);}
void glass_grok_set_reduced_motion(struct glass_grok *b,bool reduced){if(b)b->reduced=reduced;}
void glass_grok_set_obscured_by(struct glass_grok *b,lv_obj_t *overlay){if(b)b->overlay=overlay;}
unsigned glass_grok_current(const struct glass_grok *b){return b?b->current:2;}
bool glass_grok_ready(const struct glass_grok *b){return b&&b->initialized&&!b->failed;}
#ifdef GROK_HOST_TEST
void glass_grok_test_step(struct glass_grok *b){if(b)grok_tick(b->timer);}
void glass_grok_test_step_visible(void){for(unsigned i=0;i<8;i++)if(grok_test_instances[i])grok_tick(grok_test_instances[i]->timer);}
unsigned glass_grok_test_expression(struct glass_grok *b,const char *id)
{
  if(!glass_grok_ready(b))return UINT32_MAX;
  JSValue value=JS_NewString(b->context,id);
  b->deadline=grok_now()+QPK_JS_EVENT_TIMEOUT_MS;
  JSValue result=JS_Call(b->context,b->emotion,JS_UNDEFINED,1,&value);
  b->deadline=0;JS_FreeValue(b->context,value);
  int32_t selected=-1;
  if(!JS_IsException(result))JS_ToInt32(b->context,&selected,result);
  if(!grok_result(b,result))return UINT32_MAX;
  JSValue animate=JS_NewBool(b->context,false);
  bool ok=grok_call(b,b->step,1,&animate);JS_FreeValue(b->context,animate);
  return ok?(unsigned)selected:UINT32_MAX;
}
#endif
