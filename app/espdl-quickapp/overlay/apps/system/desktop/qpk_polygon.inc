/* SPDX-License-Identifier: Apache-2.0 */
static bool qpk_widget_current(int handle,uint64_t generation);
#include "qpk_polygon_draw.inc"
#define QPK_POLYGON_CURRENT(c,h,g) qpk_widget_current(h,g)
#include "qpk_polygon_convert.inc"
#undef QPK_POLYGON_CURRENT
static JSValue qpk_polygon_create(JSContext *c,int argc,JSValueConst *argv,bool packed)
{
  struct qpk_polygon_s *p=calloc(1,sizeof(*p));if(!p)return JS_ThrowOutOfMemory(c);
  lv_obj_t *obj=lv_obj_create(g_qpk.root);
  if(!obj){free(p);return JS_ThrowOutOfMemory(c);}
  lv_obj_remove_style_all(obj);lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  JSValue result=qpk_polygon_update(c,obj,p,argc,argv,0,0,0,packed);
  if(JS_IsException(result)){lv_obj_delete(obj);free(p);return result;}
  int handle=qpk_add_widget(obj,QPK_WIDGET_POLYGON);
  if(!handle){lv_obj_delete(obj);free(p);return JS_ThrowOutOfMemory(c);}
  g_qpk.widget_extra[handle-1]=p;
  lv_obj_add_event_cb(obj,qpk_polygon_draw,LV_EVENT_DRAW_MAIN,p);
  return JS_NewInt32(c,handle);
}

static JSValue qpk_polygon_set(JSContext *c,int argc,JSValueConst *argv,bool packed)
{
  int handle=qpk_arg_int(c,argc,argv,0,0);
  if(handle<=0||handle>g_qpk.widget_capacity||!g_qpk.widgets[handle-1]||
     g_qpk.widget_types[handle-1]!=QPK_WIDGET_POLYGON)return JS_ThrowRangeError(c,"invalid polygon handle");
  return qpk_polygon_update(c,g_qpk.widgets[handle-1],g_qpk.widget_extra[handle-1],argc,argv,1,handle,g_qpk.widget_generations[handle-1],packed);
}

static JSValue js_ui_polygon(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{(void)self;return qpk_polygon_create(c,argc,argv,false);}
static JSValue js_ui_polygon_set(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{(void)self;return qpk_polygon_set(c,argc,argv,false);}
static JSValue js_ui_polygon_buffer(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{(void)self;return qpk_polygon_create(c,argc,argv,true);}
static JSValue js_ui_polygon_set_buffer(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{(void)self;return qpk_polygon_set(c,argc,argv,true);}

static JSValue js_ui_front(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  (void)self;int handle=qpk_arg_int(c,argc,argv,0,0);
  if(handle<=0||handle>g_qpk.widget_capacity||!g_qpk.widgets[handle-1])return JS_ThrowRangeError(c,"invalid widget handle");
  lv_obj_move_foreground(g_qpk.widgets[handle-1]);return JS_UNDEFINED;
}
