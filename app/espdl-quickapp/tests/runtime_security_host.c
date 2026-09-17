/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <quickjs.h>
#include "qpk_security.h"
#include "qpk_error_text.h"

typedef struct lv_obj_s lv_obj_t;
typedef struct { lv_obj_t *target; void *data; } lv_event_t;
struct lv_obj_s { bool alive; int x,y,w,h,radius; void (*deleted)(lv_event_t *); void *data; };
enum qpk_widget_type_e { QPK_WIDGET_LABEL, QPK_WIDGET_NUMBER, QPK_WIDGET_PANEL, QPK_WIDGET_BUTTON, QPK_WIDGET_RECT };
struct {
  JSContext *context;
  lv_obj_t **widgets;
  uint8_t *widget_types;
  uint64_t *widget_generations;
  uint64_t widget_serial;
  int widget_capacity, widget_hint;
  char error[160];
  void (*toast_cb)(const char *);
  const void *(*font_cb)(int);
} g_qpk;
static char g_qpk_last_error[256];
#define LV_EVENT_DELETE 1
#define LV_STATE_DISABLED 1
#define LV_TEXT_ALIGN_CENTER 1
#define LV_TEXT_ALIGN_LEFT 0
#define LV_LABEL_LONG_DOT 1
#define LV_LABEL_LONG_WRAP 0

static size_t strlcpy(char *out,const char *in,size_t n)
{ size_t size=strlen(in); if(n){size_t c=size<n-1?size:n-1;memcpy(out,in,c);out[c]=0;}return size; }
static void *lv_event_get_user_data(lv_event_t *e) { return e->data; }
static lv_obj_t *lv_event_get_target(lv_event_t *e) { return e->target; }
static void *lv_obj_add_event_cb(lv_obj_t *o,void (*cb)(lv_event_t *),int code,void *data)
{ (void)code; o->deleted=cb;o->data=data;return o; }
static void lv_obj_delete(lv_obj_t *o)
{ assert(o&&o->alive); if(o->deleted){lv_event_t e={o,o->data};o->deleted(&e);}o->alive=false;free(o); }
static lv_obj_t *lv_obj_get_child(lv_obj_t *o,int i) { assert(o&&o->alive);(void)i;return NULL; }
static uint32_t lv_color_hex(uint32_t c) { return c; }
#define STYLE(name,type) static void name(lv_obj_t *o,type value,int selector) { assert(o&&o->alive);(void)value;(void)selector; }
STYLE(lv_obj_set_style_shadow_width,int)
STYLE(lv_obj_set_style_border_color,uint32_t)
STYLE(lv_obj_set_style_border_width,int)
STYLE(lv_obj_set_style_border_opa,int)
STYLE(lv_obj_set_style_text_color,uint32_t)
STYLE(lv_obj_set_style_text_font,const void *)
STYLE(lv_obj_set_style_text_align,int)
static void lv_obj_set_style_radius(lv_obj_t *o,int value,int selector)
{ assert(o&&o->alive);(void)selector;o->radius=value; }
static void lv_label_set_long_mode(lv_obj_t *o,int value) { assert(o&&o->alive);(void)value; }
static void lv_obj_remove_state(lv_obj_t *o,int value) { assert(o&&o->alive);(void)value; }
static void lv_obj_add_state(lv_obj_t *o,int value) { assert(o&&o->alive);(void)value; }
static void lv_obj_set_pos(lv_obj_t *o,int x,int y) { assert(o&&o->alive);o->x=x;o->y=y; }
static void lv_obj_set_size(lv_obj_t *o,int w,int h) { assert(o&&o->alive);o->w=w;o->h=h; }

/* Exact production function bodies are extracted by run_security.py. */
#include "runtime_subset.inc"

static void run(JSContext *ctx,const char *source)
{
  JSValue result=JS_Eval(ctx,source,strlen(source),"regression",JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(result)){qpk_show_error("test failed");abort();}
  JS_FreeValue(ctx,result);
}
static int number(JSContext *ctx,const char *source)
{
  int32_t out;
  JSValue result=JS_Eval(ctx,source,strlen(source),"assertion",JS_EVAL_TYPE_GLOBAL);
  assert(!JS_IsException(result)&&JS_ToInt32(ctx,&out,result)==0);
  JS_FreeValue(ctx,result);return out;
}
static void error_case(JSContext *ctx,const char *source,bool hooks)
{
  run(ctx,"globalThis.hooks=0");
  JSValue result=JS_Eval(ctx,source,strlen(source),"exception-test",JS_EVAL_TYPE_GLOBAL);
  assert(JS_IsException(result));
  qpk_show_error("expected");JS_FreeValue(ctx,result);
  assert((number(ctx,"hooks")>0)==hooks);
}
#ifndef BASELINE_ONLY
static JSValue make(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv)
{
  (void)self;(void)argc;(void)argv;
  lv_obj_t *obj=calloc(1,sizeof(*obj));assert(obj);obj->alive=true;
  int id=qpk_add_widget(obj,QPK_WIDGET_PANEL);assert(id>0);return JS_NewInt32(ctx,id);
}
static JSValue radius(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv)
{
  (void)self;int h=qpk_arg_int(ctx,argc,argv,0,0);
  assert(h>0&&h<=g_qpk.widget_capacity&&g_qpk.widgets[h-1]);
  return JS_NewInt32(ctx,g_qpk.widgets[h-1]->radius);
}
#endif

int main(void)
{
  JSRuntime *rt=JS_NewRuntime();assert(rt);
  JSContext *ctx=JS_NewContext(rt);assert(ctx);g_qpk.context=ctx;
  run(ctx,"globalThis.hooks=0");
#ifdef BASELINE_ONLY
  bool expect_hooks=true;
#else
  bool expect_hooks=false;
#endif
  error_case(ctx,"throw {toString(){hooks++;return 'object'},get stack(){hooks++;return 'stack'}}",expect_hooks);
  error_case(ctx,"var e=new Error('normal');Object.defineProperty(e,'message',{get(){hooks++;return 'getter'}});throw e",expect_hooks);
  error_case(ctx,"throw new Proxy(new Error('proxy'),{get(t,p){hooks++;return Reflect.get(t,p)},getOwnPropertyDescriptor(t,p){hooks++;return Reflect.getOwnPropertyDescriptor(t,p)}})",expect_hooks);
  error_case(ctx,"throw 'plain message'",false);
  assert(strstr(g_qpk_last_error,"plain message"));
  error_case(ctx,"throw new Error('normal error')",false);
  assert(strstr(g_qpk_last_error,"normal error"));
#ifndef BASELINE_ONLY
  assert(!qpk_launch_identity_valid("com.openvela.homeassistant","/data/qpk/evil/app.js",48));
  assert(!qpk_launch_identity_valid("com.openvela.homeassistant",NULL,48));
  assert(!qpk_launch_identity_valid("com.openvela.homeassistant","builtin:/homeassistant/app.js/extra",48));
  assert(qpk_launch_identity_valid("com.openvela.homeassistant","builtin:/homeassistant/app.js",48));
  assert(qpk_builtin_ha_origin("com.openvela.homeassistant","builtin:/homeassistant/app.js"));
  assert(!qpk_builtin_ha_origin("org.flash.dafeiyu","builtin:/homeassistant/app.js"));
  assert(qpk_launch_identity_valid("com.openvela.esphome.ha","/data/qpk/espha/app.js",48));
  assert(qpk_launch_identity_valid(NULL,NULL,48));
  char oversized[64];memset(oversized,'a',sizeof(oversized));oversized[63]=0;
  assert(!qpk_launch_identity_valid(oversized,"/data/qpk/app.js",48));
  char embedded[]="com.openvela.homeassistant\0suffix";
  assert(!qpk_launch_identity_valid(embedded,"/data/qpk/app.js",48));
  JSValue global=JS_GetGlobalObject(ctx),ui=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,ui,"make",JS_NewCFunction(ctx,make,"make",0));
  JS_SetPropertyStr(ctx,ui,"remove",JS_NewCFunction(ctx,js_ui_remove,"remove",1));
  JS_SetPropertyStr(ctx,ui,"radius",JS_NewCFunction(ctx,radius,"radius",1));
  JS_SetPropertyStr(ctx,ui,"setStyle",JS_NewCFunction(ctx,js_ui_set_style,"setStyle",2));
  JS_SetPropertyStr(ctx,ui,"setPos",JS_NewCFunction(ctx,js_ui_set_pos,"setPos",3));
  JS_SetPropertyStr(ctx,ui,"setSize",JS_NewCFunction(ctx,js_ui_set_size,"setSize",3));
  JS_SetPropertyStr(ctx,global,"ui",ui);JS_FreeValue(ctx,global);
  run(ctx,"function expect(v){if(!v)throw Error('assertion failed')}\n"
      "for(let n=0;n<100;n++){let h=ui.make(),fresh=0,caught=false;try{ui.setStyle(h,{get radius(){ui.remove(h);fresh=ui.make();return 8}})}catch(e){caught=true}expect(caught);expect(h===fresh);expect(ui.radius(fresh)===0);ui.remove(fresh)}\n"
      "for(const method of ['setPos','setSize']){let h=ui.make(),fresh=0,caught=false;try{ui[method](h,{valueOf(){ui.remove(h);fresh=ui.make();return 9}},10)}catch(e){caught=true}expect(caught);ui.remove(fresh)}\n"
      "let a=ui.make(),b=ui.make();ui.setStyle(a,{get radius(){ui.remove(b);return 7}});expect(ui.radius(a)===7);\n"
      "let caught=false;try{ui.setSize(a,{valueOf(){throw Error('conversion')}},10)}catch(e){caught=true}expect(caught);ui.setPos(a,20,30);ui.setSize(a,50,60);ui.remove(a);\n"
      "let list=[];for(let i=0;i<80;i++)list.push(ui.make());for(const id of list)ui.remove(id)");
  for(int i=0;i<g_qpk.widget_capacity;i++)assert(!g_qpk.widgets[i]);
  free(g_qpk.widgets);free(g_qpk.widget_types);free(g_qpk.widget_generations);
#endif
  JS_FreeContext(ctx);JS_FreeRuntime(rt);
#ifdef BASELINE_ONLY
  puts("BASELINE: attacker-controlled exception hooks executed");
#else
  puts("PASS: real QuickJS exception hooks blocked; legitimate errors preserved; reserved identity enforced; production widget setters reject deletion/reuse; native LVGL operations are stubs");
#endif
  return 0;
}
