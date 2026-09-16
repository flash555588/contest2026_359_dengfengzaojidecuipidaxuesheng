/* SPDX-License-Identifier: Apache-2.0 */
/* Included by qpk_runtime.c; every function runs on the LVGL thread. */
static lv_obj_t *g_dl_view;
static uint16_t *g_dl_view_pixels;
static void qpk_dl_canvas_deleted(lv_event_t *event)
{
  if (lv_event_get_target(event)==g_dl_view) {
    g_dl_view=NULL;free(g_dl_view_pixels);g_dl_view_pixels=NULL;
  }
}
static void qpk_dl_ui_stop(void)
{
  qpk_dl_cancel();
  if (g_dl_view) lv_obj_delete(g_dl_view);
}
static JSValue js_dl_canvas(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  if (g_dl_view) return JS_ThrowInternalError(c,"Preview already exists");
  uint16_t *pixels=calloc(QPK_DL_PIXELS,sizeof(uint16_t));
  if (!pixels) return JS_ThrowOutOfMemory(c);
  lv_obj_t *canvas=lv_canvas_create(g_qpk.root);
  if (!canvas) {free(pixels);return JS_ThrowOutOfMemory(c);}
  int handle=qpk_add_widget(canvas,QPK_WIDGET_PANEL);
  if (!handle) {lv_obj_delete(canvas);free(pixels);return JS_ThrowInternalError(c,"Too many widgets");}
  lv_canvas_set_buffer(canvas,pixels,QPK_DL_WIDTH,QPK_DL_HEIGHT,LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(canvas,qpk_arg_int(c,argc,argv,0,24),qpk_arg_int(c,argc,argv,1,102));
  /* Scale only the displayed image. Model input and frame ownership stay fixed. */
  int width=qpk_arg_int(c,argc,argv,2,QPK_DL_WIDTH);
  if (width<QPK_DL_WIDTH) width=QPK_DL_WIDTH;
  if (width>480) width=480;
  lv_image_set_pivot(canvas,0,0);
  lv_image_set_scale(canvas,width*256/QPK_DL_WIDTH);
  lv_image_set_antialias(canvas,false);
  lv_obj_remove_flag(canvas,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
  g_dl_view=canvas;g_dl_view_pixels=pixels;
  lv_obj_add_event_cb(canvas,qpk_dl_canvas_deleted,LV_EVENT_DELETE,NULL);
  return JS_NewInt32(c,handle);
}
static JSValue js_dl_start(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  struct qpk_dl_request request={0};
  int mode=qpk_arg_int(c,argc,argv,0,-1);
  if (mode<0 || mode>QPK_DL_FACE) return JS_ThrowRangeError(c,"Unknown inference mode");
  request.mode=mode;
  pthread_mutex_lock(&g_camera_lock);
  bool busy=g_camera_state!=QPK_CAMERA_IDLE;
  pthread_mutex_unlock(&g_camera_lock);
  if (busy) return JS_NewInt32(c,-EBUSY);
  const char *id=qpk_arg_string(c,argc,argv,1);
  if (!id) return JS_EXCEPTION;
  int device=qpk_usb_index(id);JS_FreeCString(c,id);
  if (device<0) return JS_NewInt32(c,-ENODEV);
  request.device=device;
  if (argc<3 || JS_ToUint32(c,&request.generation,argv[2])<0) return JS_EXCEPTION;
  return JS_NewInt32(c,qpk_dl_start(&request));
}
static JSValue js_dl_cancel(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{qpk_dl_cancel();return JS_UNDEFINED;}
static JSValue js_dl_status(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  struct qpk_dl_result result;qpk_dl_status(&result);
  JSValue obj=JS_NewObject(c),items=JS_NewArray(c),track=JS_NewObject(c);
  JS_SetPropertyStr(c,obj,"request",JS_NewUint32(c,result.request));
  JS_SetPropertyStr(c,obj,"frame",JS_NewUint32(c,result.frame));
  JS_SetPropertyStr(c,obj,"busy",JS_NewBool(c,result.busy));
  JS_SetPropertyStr(c,obj,"preview",JS_NewBool(c,result.preview));
  qpk_js_int(c,obj,"stage",result.stage);qpk_js_int(c,obj,"mode",result.mode);
  qpk_js_int(c,obj,"error",result.error);qpk_js_int(c,obj,"elapsedMs",result.elapsed_ms);
  qpk_js_int(c,track,"state",result.track.state);qpk_js_int(c,track,"target",result.track.target);
  JS_SetPropertyStr(c,track,"id",JS_NewUint32(c,result.track.id));
  qpk_js_int(c,track,"offsetX",result.track.offset_x);qpk_js_int(c,track,"offsetY",result.track.offset_y);
  JS_SetPropertyStr(c,obj,"track",track);
  for (unsigned i=0;i<result.count && i<QPK_DL_MAX_RESULTS;i++) {
    struct qpk_dl_item *item=&result.items[i];JSValue o=JS_NewObject(c);
    qpk_js_string(c,o,"label",item->label);JS_SetPropertyStr(c,o,"score",JS_NewFloat64(c,item->score));
    qpk_js_int(c,o,"x1",item->x1);qpk_js_int(c,o,"y1",item->y1);
    qpk_js_int(c,o,"x2",item->x2);qpk_js_int(c,o,"y2",item->y2);
    JS_SetPropertyUint32(c,items,i,o);
  }
  JS_SetPropertyStr(c,obj,"items",items);return obj;
}
static void qpk_dl_box_pixel(int x,int y,uint16_t color)
{
  if (x>=0 && x<QPK_DL_WIDTH && y>=0 && y<QPK_DL_HEIGHT) g_dl_view_pixels[y*QPK_DL_WIDTH+x]=color;
}
static JSValue js_dl_render(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  uint32_t id=0,frame=0;
  if (argc<2 || JS_ToUint32(c,&id,argv[0])<0 || JS_ToUint32(c,&frame,argv[1])<0) return JS_EXCEPTION;
  struct qpk_dl_result result;
  if (!g_dl_view || qpk_dl_copy_preview(id,frame,g_dl_view_pixels,QPK_DL_PIXELS,&result)<0) return JS_FALSE;
  if (result.mode==QPK_DL_FACE) {
    for (int n=-6;n<=6;n++) {
      qpk_dl_box_pixel(QPK_DL_WIDTH/2+n,QPK_DL_HEIGHT/2,0xffff);
      qpk_dl_box_pixel(QPK_DL_WIDTH/2,QPK_DL_HEIGHT/2+n,0xffff);
    }
    for (unsigned i=0;i<result.count && i<QPK_DL_MAX_RESULTS;i++) {
      struct qpk_dl_item *b=&result.items[i];
      uint16_t color=result.track.target==(int)i?0x07e0:0xffe0;
      for (int n=0;n<2;n++) {
        for (int x=b->x1;x<=b->x2;x++) {qpk_dl_box_pixel(x,b->y1+n,color);qpk_dl_box_pixel(x,b->y2-n,color);}
        for (int y=b->y1;y<=b->y2;y++) {qpk_dl_box_pixel(b->x1+n,y,color);qpk_dl_box_pixel(b->x2-n,y,color);}
      }
    }
  }
  lv_obj_invalidate(g_dl_view);return JS_TRUE;
}
static JSValue js_dl_clear(JSContext *c,JSValueConst self,int argc,JSValueConst *argv)
{
  if (g_dl_view) {memset(g_dl_view_pixels,0,QPK_DL_PIXELS*sizeof(uint16_t));lv_obj_invalidate(g_dl_view);}
  return JS_UNDEFINED;
}
static void qpk_dl_install(JSContext *c,JSValue system)
{
  JSValue o=JS_NewObject(c);
  JS_SetPropertyStr(c,o,"preview",JS_NewCFunction(c,js_dl_canvas,"preview",3));
  JS_SetPropertyStr(c,o,"clear",JS_NewCFunction(c,js_dl_clear,"clear",0));
  JS_SetPropertyStr(c,o,"start",JS_NewCFunction(c,js_dl_start,"start",3));
  JS_SetPropertyStr(c,o,"cancel",JS_NewCFunction(c,js_dl_cancel,"cancel",0));
  JS_SetPropertyStr(c,o,"status",JS_NewCFunction(c,js_dl_status,"status",0));
  JS_SetPropertyStr(c,o,"render",JS_NewCFunction(c,js_dl_render,"render",2));
  JS_SetPropertyStr(c,system,"espdl",o);
}
