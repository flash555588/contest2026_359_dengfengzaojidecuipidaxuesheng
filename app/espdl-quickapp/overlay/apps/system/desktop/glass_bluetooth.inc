/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_ble_central.h"

enum {BLE_LIST_SCAN=1,BLE_LIST_CONNECT,BLE_LIST_CANCEL,BLE_LIST_DISCONNECT,BLE_LIST_MOUSE};
struct ble_list_job {
  unsigned operation,index;
  uint32_t scan_generation,generation;
  bool peer_valid;
  struct glass_ble_device peer;
};
/* No page/object pointers cross this mailbox. The desktop worker only calls
 * the public BLE boundary; GAP/GATT remain on the persistent host task group. */
static pthread_mutex_t g_ble_list_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ble_list_wake=PTHREAD_COND_INITIALIZER;
static struct {
  bool started,busy,pending,cancel;
  unsigned operation;
  int result;
  struct ble_list_job job;
} g_ble_list_jobs;

struct ble_list_row {
  lv_obj_t *root,*name,*address,*signal,*action,*arrow;
  struct ble_list_job binding;
};
struct ble_list_ui {
  lv_obj_t *root,*status,*detail,*scan,*control,*list,*count,*empty;
  struct ble_list_row rows[GLASS_BLE_LIMIT];
  struct glass_ble_state state,previous;
  struct glass_ble_hid_state hid;
  struct ble_list_job pressed;
  lv_obj_t *pressed_row;
  lv_timer_t *timer;
  unsigned action,previous_operation;
  int previous_result;
  bool valid,previous_busy,previous_hid;
};
static struct ble_list_ui *g_ble_list;
static void glass_ble_list_refresh(lv_timer_t *timer);

static bool ble_list_same_peer(const struct glass_ble_device *a,const struct glass_ble_device *b)
{return a->type==b->type&&!memcmp(a->address,b->address,6);}
static bool ble_list_cancelled(void)
{
  pthread_mutex_lock(&g_ble_list_lock);bool cancel=g_ble_list_jobs.cancel;
  pthread_mutex_unlock(&g_ble_list_lock);return cancel;
}
static int ble_list_wait_ready(void)
{
  if(ble_list_cancelled())return -ECANCELED;
  int result=glass_ble_start();if(result)return result;
  for(unsigned i=0;i<1500;i++){
    if(ble_list_cancelled())return -ECANCELED;
    struct glass_ble_state state;glass_ble_read(&state);
    if(state.ready)return 0;
    if(state.error)return state.error;
    usleep(100000);
  }
  return -ETIMEDOUT;
}
static int ble_list_execute(const struct ble_list_job *job)
{
  if(job->operation==BLE_LIST_SCAN||job->operation==BLE_LIST_CONNECT){
    int result=ble_list_wait_ready();if(result)return result;
  }
  struct glass_ble_state state;glass_ble_read(&state);
  if(ble_list_cancelled())return -ECANCELED;
  switch(job->operation){
  case BLE_LIST_SCAN:return glass_ble_scan();
  case BLE_LIST_CONNECT:
    if(state.scan_generation!=job->scan_generation||job->index>=state.count||
       job->index>=GLASS_BLE_LIMIT||!ble_list_same_peer(&state.devices[job->index],&job->peer))return -ESTALE;
    if(state.scanning){int result=glass_ble_cancel();if(result)return result;}
    if(ble_list_cancelled())return -ECANCELED;
    /* The host validates the scan generation again after the scan stops. */
    return glass_ble_central_connect(job->index,job->scan_generation);
  case BLE_LIST_CANCEL:
  case BLE_LIST_DISCONNECT:
    if(state.generation!=job->generation||state.scan_generation!=job->scan_generation||
       (job->peer_valid&&(!state.peer_valid||!ble_list_same_peer(&state.peer,&job->peer))))return -ESTALE;
    return job->operation==BLE_LIST_CANCEL?glass_ble_cancel():glass_ble_disconnect();
  default:return -EINVAL;
  }
}
static void *glass_ble_list_worker(void *unused)
{
  LV_UNUSED(unused);
  for(;;){
    pthread_mutex_lock(&g_ble_list_lock);
    while(!g_ble_list_jobs.pending)pthread_cond_wait(&g_ble_list_wake,&g_ble_list_lock);
    struct ble_list_job job=g_ble_list_jobs.job;g_ble_list_jobs.pending=false;
    pthread_mutex_unlock(&g_ble_list_lock);
    int result=ble_list_execute(&job);
    pthread_mutex_lock(&g_ble_list_lock);bool cancelled=g_ble_list_jobs.cancel;
    if(!cancelled||result||(job.operation!=BLE_LIST_SCAN&&job.operation!=BLE_LIST_CONNECT)){
      g_ble_list_jobs.result=result;g_ble_list_jobs.busy=false;g_ble_list_jobs.cancel=false;
      pthread_mutex_unlock(&g_ble_list_lock);continue;
    }
    pthread_mutex_unlock(&g_ble_list_lock);
    {
      struct glass_ble_state state;glass_ble_read(&state);
      if(job.operation==BLE_LIST_CONNECT&&state.peer_valid&&state.scan_generation==job.scan_generation&&
         ble_list_same_peer(&state.peer,&job.peer)){
        if(state.connected&&state.generation==job.generation+1)(void)glass_ble_disconnect();
        else if(state.connecting&&state.generation==job.generation)(void)glass_ble_cancel();
      }else if(job.operation==BLE_LIST_SCAN&&state.scanning&&!state.connected&&!state.connecting&&
               state.scan_generation==job.scan_generation+1)(void)glass_ble_cancel();
      result=-ECANCELED;
    }
    pthread_mutex_lock(&g_ble_list_lock);
    g_ble_list_jobs.result=result;g_ble_list_jobs.busy=false;g_ble_list_jobs.cancel=false;
    pthread_mutex_unlock(&g_ble_list_lock);
  }
  return NULL;
}
static int glass_ble_list_submit(const struct ble_list_job *job)
{
  pthread_mutex_lock(&g_ble_list_lock);
  if(g_ble_list_jobs.busy){
    if(job->operation==BLE_LIST_CANCEL&&
       (g_ble_list_jobs.operation==BLE_LIST_SCAN||g_ble_list_jobs.operation==BLE_LIST_CONNECT)){
      g_ble_list_jobs.cancel=true;pthread_mutex_unlock(&g_ble_list_lock);return 0;
    }
    pthread_mutex_unlock(&g_ble_list_lock);return -EBUSY;
  }
  int result=0;
  if(!g_ble_list_jobs.started){
    pthread_attr_t attr;pthread_t thread;result=pthread_attr_init(&attr);
    if(!result){
      result=pthread_attr_setstacksize(&attr,16384);
      if(!result)result=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
      if(!result)result=pthread_create(&thread,&attr,glass_ble_list_worker,NULL);
      pthread_attr_destroy(&attr);
    }
    if(!result)g_ble_list_jobs.started=true;
  }
  if(!result){
    g_ble_list_jobs.job=*job;g_ble_list_jobs.operation=job->operation;
    g_ble_list_jobs.busy=g_ble_list_jobs.pending=true;g_ble_list_jobs.cancel=false;g_ble_list_jobs.result=0;
    pthread_cond_signal(&g_ble_list_wake);
  }
  pthread_mutex_unlock(&g_ble_list_lock);return result?-result:0;
}
static const char *ble_list_error(int error)
{
  switch(error){
  case -ECANCELED:return "操作已取消";
  case -ESTALE:return "设备列表已更新，请重新选择设备";
  case -EBUSY:return "蓝牙正在被其他操作使用，请稍后重试";
  case -ETIMEDOUT:return "操作超时，请靠近设备并重试";
  case -ENOMEM:return "内存不足，请稍后重试";
  case -ENOTCONN:return "设备已断开，可重新搜索连接";
  default:return "蓝牙操作失败，请检查设备后重试";
  }
}
static void ble_list_text(lv_obj_t *label,const char *text)
{if(strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);}
static void ble_list_address(const struct glass_ble_device *peer,char *text,size_t size)
{
  snprintf(text,size,"%02X:%02X:%02X:%02X:%02X:%02X · %s",peer->address[5],peer->address[4],
    peer->address[3],peer->address[2],peer->address[1],peer->address[0],peer->type&1?"随机地址":"公共地址");
}
static void glass_ble_list_action(lv_event_t *event)
{
  struct ble_list_ui *view=g_ble_list;if(!view)return;
  unsigned operation=(uintptr_t)lv_event_get_user_data(event);
  if(lv_obj_has_state(lv_event_get_target(event),LV_STATE_DISABLED))return;
  if(!operation)operation=view->action;
  if(operation==BLE_LIST_MOUSE){glass_mouse_open(NULL);return;}
  struct ble_list_job job={.operation=operation,.generation=view->state.generation,
    .scan_generation=view->state.scan_generation,.peer_valid=view->state.peer_valid,.peer=view->state.peer};
  int result=glass_ble_list_submit(&job);if(result)show_toast(ble_list_error(result));
  glass_ble_list_refresh(NULL);
}
static void glass_ble_list_connect(lv_event_t *event)
{
  struct ble_list_ui *view=g_ble_list;if(!view)return;
  struct ble_list_row *row=lv_event_get_user_data(event);
  if(lv_obj_has_state(row->root,LV_STATE_DISABLED))return;
  if(lv_event_get_code(event)==LV_EVENT_PRESSED){view->pressed=row->binding;view->pressed_row=row->root;return;}
  if(lv_event_get_code(event)!=LV_EVENT_CLICKED)return;
  if(view->pressed_row==row->root&&
     (view->pressed.scan_generation!=row->binding.scan_generation||
      !ble_list_same_peer(&view->pressed.peer,&row->binding.peer))){
    view->pressed_row=NULL;show_toast(ble_list_error(-ESTALE));return;
  }
  view->pressed_row=NULL;
  int result=glass_ble_list_submit(&row->binding);if(result)show_toast(ble_list_error(result));
  glass_ble_list_refresh(NULL);
}
static void glass_ble_list_refresh(lv_timer_t *timer)
{
  LV_UNUSED(timer);struct ble_list_ui *view=g_ble_list;
  if(!view||!glass_ui_can_paint(view->root))return;
  memset(&view->state,0,sizeof(view->state));
  glass_ble_read(&view->state);glass_ble_hid_read(&view->hid);
  /* The legacy public connected flag also includes the HID peripheral.
   * Only a central peer belongs in this list's disconnect action. */
  view->state.connected=view->state.connected&&view->state.peer_valid;
  if(view->state.count>GLASS_BLE_LIMIT)view->state.count=GLASS_BLE_LIMIT;
  for(unsigned i=0;i<view->state.count;i++)view->state.devices[i].name[GLASS_BLE_NAME_MAX]=0;
  view->state.peer.name[GLASS_BLE_NAME_MAX]=0;
  bool hid=view->hid.enabled||view->hid.advertising||view->hid.connected;
  pthread_mutex_lock(&g_ble_list_lock);
  bool busy=g_ble_list_jobs.busy;unsigned operation=g_ble_list_jobs.operation;int result=g_ble_list_jobs.result;
  pthread_mutex_unlock(&g_ble_list_lock);
  if(view->valid&&view->previous.scan_generation!=view->state.scan_generation)
    lv_obj_scroll_to_y(view->list,0,LV_ANIM_OFF);
  if(view->valid&&!memcmp(&view->previous,&view->state,sizeof(view->state))&&
     view->previous_busy==busy&&view->previous_operation==operation&&view->previous_result==result&&view->previous_hid==hid)return;
  view->valid=true;view->previous=view->state;view->previous_busy=busy;
  view->previous_operation=operation;view->previous_result=result;view->previous_hid=hid;
  struct glass_ble_state *state=&view->state;
  const char *status=state->disconnecting?"正在断开连接":state->connected?"已连接":state->connecting?"正在连接设备":
    busy?(operation==BLE_LIST_SCAN?"正在准备搜索":operation==BLE_LIST_CONNECT?"正在准备连接":"正在处理操作"):
    state->scanning?"正在搜索附近设备":hid?"无线触摸板正在使用蓝牙":"尚未连接设备";
  if(state->peer_valid&&(state->connected||state->connecting)){
    char title[144];snprintf(title,sizeof(title),"%s · %s",status,state->peer.name[0]?state->peer.name:"未命名设备");
    ble_list_text(view->status,title);
  }else ble_list_text(view->status,status);
  char detail[180];
  if(!busy&&(result||state->error))snprintf(detail,sizeof(detail),"%s",ble_list_error(result?result:state->error));
  else if(state->peer_valid&&(state->connected||state->connecting)){
    char address[80];ble_list_address(&state->peer,address,sizeof(address));
    snprintf(detail,sizeof(detail),"%s%s",address,state->connected?(state->bonded?" · 已配对":state->encrypted?" · 已加密":" · 未配对"):"");
  }else snprintf(detail,sizeof(detail),"%s",hid?"停止触摸板配对后，即可搜索其他设备":
    state->scanning?"点选设备即可停止搜索并连接":"搜索附近的低功耗蓝牙设备，设备需处于可连接状态");
  ble_list_text(view->detail,detail);
  bool can_scan=!busy&&!state->scanning&&!state->connecting&&!state->connected&&!state->disconnecting&&!hid;
  glass_mouse_button_enabled(view->scan,can_scan);
  ble_list_text(lv_obj_get_child(view->scan,0),state->scanning?"搜索中…":"搜索设备");
  view->action=state->connected?BLE_LIST_DISCONNECT:state->scanning||state->connecting||busy?BLE_LIST_CANCEL:BLE_LIST_MOUSE;
  const char *control=view->action==BLE_LIST_DISCONNECT?"断开连接":state->connecting?"取消连接":
    state->scanning?"停止搜索":busy?"取消操作":"无线触摸板";
  ble_list_text(lv_obj_get_child(view->control,0),control);
  glass_mouse_button_enabled(view->control,!state->disconnecting&&(!busy||operation==BLE_LIST_SCAN||operation==BLE_LIST_CONNECT));
  char count[64];snprintf(count,sizeof(count),"附近设备 · %u%s",state->count,state->scanning?" · 搜索中":"");
  ble_list_text(view->count,count);
  if(state->count)lv_obj_add_flag(view->empty,LV_OBJ_FLAG_HIDDEN);
  else{
    lv_obj_remove_flag(view->empty,LV_OBJ_FLAG_HIDDEN);
    ble_list_text(view->empty,state->scanning?"正在寻找设备…":hid?"触摸板启用中，暂时无法搜索":
      busy?"正在准备蓝牙，请稍候":"还没有发现设备\n点击“搜索设备”开始查找");
  }
  /* Keep discovery order stable while RSSI changes; only the active peer is
   * pinned first. Press/click bindings protect a row replaced by a new scan. */
  unsigned order[GLASS_BLE_LIMIT],used=0;
  for(unsigned i=0;i<state->count;i++)if(state->peer_valid&&ble_list_same_peer(&state->devices[i],&state->peer))order[used++]=i;
  for(unsigned i=0;i<state->count;i++)if(!state->peer_valid||!ble_list_same_peer(&state->devices[i],&state->peer))order[used++]=i;
  for(unsigned slot=0;slot<GLASS_BLE_LIMIT;slot++){
    struct ble_list_row *row=&view->rows[slot];
    if(slot>=used){lv_obj_add_flag(row->root,LV_OBJ_FLAG_HIDDEN);continue;}
    unsigned index=order[slot];const struct glass_ble_device *peer=&state->devices[index];
    row->binding=(struct ble_list_job){.operation=BLE_LIST_CONNECT,.index=index,
      .scan_generation=state->scan_generation,.generation=state->generation,.peer_valid=true,.peer=*peer};
    bool active=state->peer_valid&&ble_list_same_peer(peer,&state->peer);
    lv_obj_remove_flag(row->root,LV_OBJ_FLAG_HIDDEN);lv_obj_set_y(row->root,slot*80);
    ble_list_text(row->name,peer->name[0]?peer->name:"未命名设备");
    char address[80];ble_list_address(peer,address,sizeof(address));ble_list_text(row->address,address);
    char signal[48];
    if(peer->rssi<-127||peer->rssi>20)snprintf(signal,sizeof(signal),"信号未知");
    else snprintf(signal,sizeof(signal),"%d dBm · %s",peer->rssi,peer->rssi>=-55?"强":peer->rssi>=-75?"良好":"弱");
    ble_list_text(row->signal,signal);
    ble_list_text(row->action,active?(state->connected?"已连接":"连接中…"):"连接");
    lv_obj_set_width(row->action,active?116:96);
    if(active)lv_obj_add_flag(row->arrow,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(row->arrow,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(row->action,lv_color_hex(active?0x4f9b77:detail_accent()),0);
    glass_mouse_button_enabled(row->root,!busy&&!hid&&!state->connecting&&!state->connected&&!state->disconnecting);
    if(state->connected||state->connecting)lv_obj_set_style_opa(row->root,active?LV_OPA_COVER:LV_OPA_70,0);
  }
}
static void glass_ble_list_deleted(lv_event_t *event)
{
  struct ble_list_ui *view=lv_event_get_user_data(event);
  if(view->timer)lv_timer_delete(view->timer);
  if(g_ble_list==view)g_ble_list=NULL;
  free(view);
}
static void glass_ble_devices_page(lv_obj_t *card)
{
  struct ble_list_ui *view=calloc(1,sizeof(*view));if(!view){show_toast("内存不足");return;}
  g_ble_list=view;view->root=card;
  lv_obj_add_event_cb(card,glass_ble_list_deleted,LV_EVENT_DELETE,view);
  pthread_mutex_lock(&g_ble_list_lock);if(!g_ble_list_jobs.busy)g_ble_list_jobs.result=0;
  pthread_mutex_unlock(&g_ble_list_lock);
  detail_page(card);detail_sidebar(card,"蓝牙",LV_SYMBOL_BLUETOOTH,panel_hide);
  lv_obj_t *selected=detail_nav(card,"设备连接",LV_SYMBOL_BLUETOOTH,104,140,NULL,NULL);glass_sidebar_item(selected,true);
  lv_obj_t *mouse=detail_nav(card,"无线触摸板",LV_SYMBOL_GPS,156,140,glass_mouse_open,NULL);glass_sidebar_item(mouse,false);
  detail_text(card,"蓝牙设备",216,26,500,28,detail_ink());
  detail_text(card,"搜索附近设备，查看连接状态",216,66,720,14,detail_muted());
  lv_obj_t *summary=detail_tile(card,216,104,784,112);
  home_icon(summary,LV_SYMBOL_BLUETOOTH,20,24,24,detail_accent());
  view->status=detail_text(summary,"尚未连接设备",60,22,414,20,detail_ink());
  lv_label_set_long_mode(view->status,LV_LABEL_LONG_DOT);
  view->detail=detail_text(summary,"",20,74,744,14,detail_muted());lv_label_set_long_mode(view->detail,LV_LABEL_LONG_DOT);
  view->scan=detail_button(summary,"搜索设备",496,16,124,glass_ble_list_action,(void *)(uintptr_t)BLE_LIST_SCAN);
  view->control=detail_button(summary,"无线触摸板",632,16,132,glass_ble_list_action,NULL);
  view->count=detail_text(card,"附近设备 · 0",216,236,700,16,detail_muted());
  view->list=glass_box(card,216,272,784,278,detail_paper(),0);
  lv_obj_set_style_bg_opa(view->list,LV_OPA_TRANSP,0);
  lv_obj_add_flag(view->list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(view->list,LV_DIR_VER);
  lv_obj_set_scrollbar_mode(view->list,LV_SCROLLBAR_MODE_AUTO);
  for(unsigned i=0;i<GLASS_BLE_LIMIT;i++){
    struct ble_list_row *row=&view->rows[i];
    row->root=glass_box(view->list,0,i*80,768,72,detail_paper(),14);
    lv_obj_add_flag(row->root,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(row->root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(row->root,1,0);lv_obj_set_style_border_color(row->root,lv_color_hex(detail_line()),0);
    home_icon(row->root,LV_SYMBOL_BLUETOOTH,18,24,22,detail_accent());
    row->name=detail_text(row->root,"",60,12,366,18,detail_ink());lv_label_set_long_mode(row->name,LV_LABEL_LONG_DOT);
    row->address=detail_text(row->root,"",60,43,440,12,detail_muted());lv_label_set_long_mode(row->address,LV_LABEL_LONG_DOT);
    row->signal=detail_text(row->root,"",458,14,170,14,detail_muted());
    row->action=detail_text(row->root,"连接",632,27,96,14,detail_accent());
    row->arrow=home_icon(row->root,LV_SYMBOL_RIGHT,742,28,14,detail_accent());
    lv_obj_set_style_text_align(row->action,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_add_event_cb(row->root,glass_ble_list_connect,LV_EVENT_PRESSED,row);
    lv_obj_add_event_cb(row->root,glass_ble_list_connect,LV_EVENT_CLICKED,row);
    lv_obj_add_flag(row->root,LV_OBJ_FLAG_HIDDEN);
  }
  view->empty=detail_text(view->list,"",24,80,720,20,detail_muted());
  lv_obj_set_style_text_align(view->empty,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_add_flag(view->empty,LV_OBJ_FLAG_IGNORE_LAYOUT);
  /* Initialize actions against laid-out geometry before the first pointer
   * event, including a direct return from the touchpad page. */
  lv_obj_update_layout(card);
  view->timer=lv_timer_create(glass_ble_list_refresh,200,NULL);glass_ble_list_refresh(NULL);
}
static void glass_ble_devices_open(lv_event_t *event)
{LV_UNUSED(event);lv_obj_t *card=panel_card("蓝牙设备");if(card)glass_ble_devices_page(card);}
