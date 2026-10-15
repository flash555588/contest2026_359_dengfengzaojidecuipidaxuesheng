/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_grok.h"
static const lv_font_t *qpk_grok_font(int size){return g_qpk.font_cb?g_qpk.font_cb(size):LV_FONT_DEFAULT;}
static JSValue js_ui_grok_sprite(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  (void)self;
  struct glass_grok *b=glass_grok_create_sprite(g_qpk.root,
    qpk_arg_int(c,argc,argv,0,0),qpk_arg_int(c,argc,argv,1,0),
    qpk_arg_int(c,argc,argv,2,128),qpk_arg_int(c,argc,argv,3,128),
    qpk_arg_int(c,argc,argv,4,96),qpk_arg_int(c,argc,argv,5,0),qpk_grok_font);
  if(!b)return JS_ThrowInternalError(c,"Grok sprite allocation or bounds");
  int h=qpk_add_widget(glass_grok_root(b),QPK_WIDGET_GROK);
  if(!h){lv_obj_delete(glass_grok_root(b));return JS_ThrowOutOfMemory(c);}return JS_NewInt32(c,h);
}
static JSValue js_ui_grok_control(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  (void)self;int h=qpk_arg_int(c,argc,argv,0,0);
  if(h<1||h>g_qpk.widget_capacity||!g_qpk.widgets[h-1]||g_qpk.widget_types[h-1]!=QPK_WIDGET_GROK)return JS_ThrowRangeError(c,"invalid Grok sprite");
  struct glass_grok *b=lv_obj_get_user_data(g_qpk.widgets[h-1]);
  int op=qpk_arg_int(c,argc,argv,1,0),value=qpk_arg_int(c,argc,argv,2,2);
  if(op==0)glass_grok_select(b,value);
  else if(op==1)glass_grok_set_active(b,value!=0);
  else if(op==2)glass_grok_action(b,value);
  else if(op==3)glass_grok_set_reduced_motion(b,value!=0);
  else return JS_ThrowRangeError(c,"invalid Grok operation");
  return JS_NewInt32(c,glass_grok_current(b));
}
