/* SPDX-License-Identifier: Apache-2.0 */
/* Native clipped, inertial viewport. Only nearby card widgets are attached;
 * a one-pixel extent anchor represents the full logical content height. */
static const unsigned char qpk_viewport_tag;
struct qpk_viewport_s {lv_obj_t *extent;int height,handle;uint64_t generation;struct qpk_event_s *binding;bool notifying,pending;};
static bool qpk_viewport_object(lv_obj_t *obj)
{return obj&&lv_obj_get_user_data(obj)==&qpk_viewport_tag;}
static void qpk_viewport_origin(lv_obj_t *parent,int *x,int *y)
{
  *x=*y=0;if(qpk_viewport_object(parent)){
    *x=lv_obj_get_style_x(parent,0);*y=lv_obj_get_style_y(parent,0);
  }
}
static void qpk_viewport_notify(void *data)
{
  struct qpk_viewport_s *state=data;
  int handle=state->handle;uint64_t generation=state->generation;
  if(!g_qpk.context||!qpk_widget_current(handle,generation))return;
  state->pending=false;struct qpk_event_s *binding=state->binding;
  if(!binding||!binding->used)return;
  state->notifying=true;
  JSValue value=JS_NewInt32(g_qpk.context,lv_obj_get_scroll_y(binding->owner));
  JSValue result=qpk_call_args(binding->function,QPK_HANG_BUDGET,1,&value);
  JS_FreeValue(g_qpk.context,value);JS_FreeValue(g_qpk.context,result);
  if(qpk_widget_current(handle,generation))((struct qpk_viewport_s *)g_qpk.widget_extra[handle-1])->notifying=false;
}
static void qpk_viewport_cleanup(lv_obj_t *obj,void *extra)
{
  if(qpk_viewport_object(obj)&&extra)lv_async_call_cancel(qpk_viewport_notify,extra);
}
static void qpk_viewport_scrolled(lv_event_t *event)
{
  struct qpk_viewport_s *state=lv_event_get_user_data(event);
  if(!g_qpk.context||!state||state->notifying||state->pending)return;
  if(!qpk_widget_current(state->handle,state->generation))return;
  state->pending=true;
  if(lv_async_call(qpk_viewport_notify,state)!=LV_RESULT_OK)state->pending=false;
}
static JSValue js_ui_viewport(JSContext *context,JSValueConst this_value,int argc,JSValueConst *argv)
{
  (void)this_value;int32_t v[4]={0};
  if(argc<5||!JS_IsFunction(context,argv[4]))return JS_ThrowTypeError(context,"viewport requires a scroll callback");
  for(unsigned i=0;i<4;i++)if(JS_ToInt32(context,&v[i],argv[i])<0)return JS_EXCEPTION;
  if(v[0]<0||v[1]<0||v[0]>4096||v[1]>4096||v[2]<1||v[3]<1||v[2]>4096||v[3]>4096)return JS_ThrowRangeError(context,"invalid viewport bounds");
  struct qpk_viewport_s *state=calloc(1,sizeof(*state));if(!state)return JS_ThrowOutOfMemory(context);
  struct qpk_event_s *binding;
  for(binding=g_qpk.events;binding&&binding->used;binding=binding->next){}
  if(!binding){binding=calloc(1,sizeof(*binding));if(!binding){free(state);return JS_ThrowOutOfMemory(context);}binding->next=g_qpk.events;g_qpk.events=binding;}
  lv_obj_t *obj=lv_obj_create(g_qpk.root);if(!obj){free(state);return JS_ThrowOutOfMemory(context);}
  lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,v[0],v[1]);lv_obj_set_size(obj,v[2],v[3]);
  lv_obj_set_user_data(obj,(void *)&qpk_viewport_tag);
  lv_obj_set_style_pad_all(obj,0,0);lv_obj_set_style_border_width(obj,0,0);
  lv_obj_remove_flag(obj,LV_OBJ_FLAG_OVERFLOW_VISIBLE|LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_scroll_dir(obj,LV_DIR_VER);lv_obj_set_scrollbar_mode(obj,LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(obj,lv_color_hex(g_qpk.secondary_color),LV_PART_SCROLLBAR);
  lv_obj_set_style_width(obj,3,LV_PART_SCROLLBAR);lv_obj_set_style_bg_opa(obj,LV_OPA_40,LV_PART_SCROLLBAR);
  state->extent=lv_obj_create(obj);if(!state->extent){lv_obj_delete(obj);free(state);return JS_ThrowOutOfMemory(context);}
  lv_obj_remove_style_all(state->extent);
  lv_obj_remove_flag(state->extent,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(state->extent,1,1);state->height=v[3];lv_obj_set_y(state->extent,v[3]-1);
  int handle=qpk_add_widget(obj,QPK_WIDGET_PANEL);
  if(!handle){lv_obj_delete(obj);free(state);return JS_ThrowOutOfMemory(context);}
  g_qpk.widget_extra[handle-1]=state;state->handle=handle;state->generation=g_qpk.widget_generations[handle-1];state->binding=binding;
  binding->owner=obj;binding->function=JS_DupValue(context,argv[4]);binding->used=true;
  if(!lv_obj_add_event_cb(obj,qpk_event_deleted,LV_EVENT_DELETE,binding)||
     !lv_obj_add_event_cb(obj,qpk_viewport_scrolled,LV_EVENT_SCROLL,state)||
     !lv_obj_add_event_cb(obj,qpk_viewport_scrolled,LV_EVENT_SCROLL_END,state)){
    qpk_event_release(context,binding);lv_obj_delete(obj);return JS_ThrowOutOfMemory(context);
  }
  return JS_NewInt32(context,handle);
}
static JSValue js_ui_attach(JSContext *context,JSValueConst this_value,int argc,JSValueConst *argv)
{
  (void)this_value;int32_t handle=0,parent=0;
  if(argc<2)return JS_ThrowTypeError(context,"attach requires child and parent handles");
  if(JS_ToInt32(context,&handle,argv[0])<0)return JS_EXCEPTION;
  if(handle<=0||handle>g_qpk.widget_capacity||!g_qpk.widgets[handle-1])return JS_ThrowRangeError(context,"invalid child handle");
  uint64_t generation=g_qpk.widget_generations[handle-1];
  if(JS_ToInt32(context,&parent,argv[1])<0)return JS_EXCEPTION;
  if(!qpk_widget_current(handle,generation))return JS_ThrowRangeError(context,"child changed during parent conversion");
  lv_obj_t *obj=g_qpk.widgets[handle-1],*target=g_qpk.root;
  if(parent){
    if(parent<=0||parent>g_qpk.widget_capacity||!qpk_viewport_object(g_qpk.widgets[parent-1]))return JS_ThrowRangeError(context,"invalid viewport handle");
    target=g_qpk.widgets[parent-1];
  }
  if(qpk_viewport_object(obj))return JS_ThrowTypeError(context,"viewports cannot be nested");
  lv_obj_t *previous=lv_obj_get_parent(obj);if(previous==target)return JS_UNDEFINED;
  if(previous!=g_qpk.root&&!qpk_viewport_object(previous))return JS_ThrowTypeError(context,"unsupported widget parent");
  int ox,oy,nx,ny;qpk_viewport_origin(previous,&ox,&oy);qpk_viewport_origin(target,&nx,&ny);
  int64_t x=(int64_t)lv_obj_get_style_x(obj,0)+ox-nx,y=(int64_t)lv_obj_get_style_y(obj,0)+oy-ny;
  if(x<INT32_MIN||x>INT32_MAX||y<INT32_MIN||y>INT32_MAX)return JS_ThrowRangeError(context,"parent coordinates exceed limits");
  lv_obj_set_parent(obj,target);lv_obj_set_pos(obj,(int)x,(int)y);
  lv_obj_add_flag(obj,LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  return JS_UNDEFINED;
}
static JSValue js_ui_scroll_info(JSContext *context,JSValueConst this_value,int argc,JSValueConst *argv)
{
  (void)this_value;int handle=qpk_arg_int(context,argc,argv,0,0);
  if(handle<=0||handle>g_qpk.widget_capacity||!qpk_viewport_object(g_qpk.widgets[handle-1]))return JS_ThrowRangeError(context,"invalid viewport handle");
  lv_obj_t *obj=g_qpk.widgets[handle-1];struct qpk_viewport_s *state=g_qpk.widget_extra[handle-1];
  JSValue result=JS_NewObject(context);
  JS_SetPropertyStr(context,result,"y",JS_NewInt32(context,lv_obj_get_scroll_y(obj)));
  JS_SetPropertyStr(context,result,"max",JS_NewInt32(context,state->height>lv_obj_get_style_height(obj,0)?state->height-lv_obj_get_style_height(obj,0):0));
  JS_SetPropertyStr(context,result,"moving",JS_NewBool(context,lv_obj_is_scrolling(obj)));
  return result;
}
static JSValue js_ui_scroll_to(JSContext *context,JSValueConst this_value,int argc,JSValueConst *argv)
{
  (void)this_value;int handle=qpk_arg_int(context,argc,argv,0,0);int32_t y=0;
  if(handle<=0||handle>g_qpk.widget_capacity||!qpk_viewport_object(g_qpk.widgets[handle-1]))return JS_ThrowRangeError(context,"invalid viewport handle");
  uint64_t generation=g_qpk.widget_generations[handle-1];
  if(argc>1&&JS_ToInt32(context,&y,argv[1])<0)return JS_EXCEPTION;
  if(!qpk_widget_current(handle,generation))return JS_ThrowRangeError(context,"viewport changed during scroll conversion");
  lv_obj_t *obj=g_qpk.widgets[handle-1];struct qpk_viewport_s *state=g_qpk.widget_extra[handle-1];
  int max=state->height-lv_obj_get_style_height(obj,0);if(max<0)max=0;if(y<0)y=0;if(y>max)y=max;
  bool animate=argc>2&&JS_IsBool(argv[2])&&JS_ToBool(context,argv[2]);
  lv_obj_scroll_to_y(obj,y,animate?LV_ANIM_ON:LV_ANIM_OFF);return JS_UNDEFINED;
}
static JSValue js_ui_scroll_extent(JSContext *context,JSValueConst this_value,int argc,JSValueConst *argv)
{
  (void)this_value;int handle=qpk_arg_int(context,argc,argv,0,0);int32_t height=0;
  if(handle<=0||handle>g_qpk.widget_capacity||!qpk_viewport_object(g_qpk.widgets[handle-1]))return JS_ThrowRangeError(context,"invalid viewport handle");
  uint64_t generation=g_qpk.widget_generations[handle-1];
  if(argc<2)return JS_ThrowTypeError(context,"content height is required");
  if(JS_ToInt32(context,&height,argv[1])<0)return JS_EXCEPTION;
  if(!qpk_widget_current(handle,generation))return JS_ThrowRangeError(context,"viewport changed during extent conversion");
  if(height<1||height>200000)return JS_ThrowRangeError(context,"invalid content height");
  struct qpk_viewport_s *state=g_qpk.widget_extra[handle-1];
  if(state->height!=height){state->height=height;lv_obj_set_y(state->extent,height-1);}
  return JS_UNDEFINED;
}
static JSValue js_ui_scroll_lock(JSContext *context,JSValueConst this_value,int argc,JSValueConst *argv)
{
  (void)this_value;int handle=qpk_arg_int(context,argc,argv,0,0);
  if(handle<=0||handle>g_qpk.widget_capacity||!qpk_viewport_object(g_qpk.widgets[handle-1]))return JS_ThrowRangeError(context,"invalid viewport handle");
  if(argc>1&&JS_IsBool(argv[1])&&JS_ToBool(context,argv[1]))lv_obj_remove_flag(g_qpk.widgets[handle-1],LV_OBJ_FLAG_SCROLLABLE);
  else lv_obj_add_flag(g_qpk.widgets[handle-1],LV_OBJ_FLAG_SCROLLABLE);
  return JS_UNDEFINED;
}
