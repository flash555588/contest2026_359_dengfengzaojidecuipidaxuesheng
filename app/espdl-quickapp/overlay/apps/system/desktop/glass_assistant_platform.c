/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_assistant.h"
#include "glass_vision.h"
#include "glass_agent.h"
#include "glass_board.h"
#include "glass_chat.h"
#include "glass_music.h"
#include "glass_weather.h"
#include "glass_speech.h"
#include "glass_status.h"
#include "qpk_espdl.h"
#include "qpk_photo_store.h"
#include "../hass/hass_actions.h"
#ifdef CONFIG_SYSTEM_C6_DESKTOP
#include "c6net.h"
#include "link_state.h"
#include <netutils/netlib.h>
#include <arpa/inet.h>
#endif
#include <nuttx/video/uvc_camera.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <time.h>
#include <unistd.h>

static const cJSON *get(const cJSON *o,const char *key){return cJSON_GetObjectItemCaseSensitive(o,key);}
static const char *string(const cJSON *o,const char *key){const cJSON *v=get(o,key);return cJSON_IsString(v)?v->valuestring:"";}
static bool number(const cJSON *v,double low,double high)
{return cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=low&&v->valuedouble<=high&&floor(v->valuedouble)==v->valuedouble;}
static bool keys(const cJSON *args,const char *allowed)
{
  if(!cJSON_IsObject(args))return false;
  for(const cJSON *v=args->child;v;v=v->next){char key[48];
    if(!v->string||strlen(v->string)>40||get(args,v->string)!=v)return false;
    snprintf(key,sizeof(key),",%s,",v->string);if(!strstr(allowed,key))return false;
  }return true;
}
static cJSON *result(bool ok,const char *error)
{cJSON *o=cJSON_CreateObject();if(o){cJSON_AddBoolToObject(o,"ok",ok);if(error)cJSON_AddStringToObject(o,"error",error);}return o;}
static char *done(cJSON *o){char *s=o?cJSON_PrintUnformatted(o):NULL;cJSON_Delete(o);return s;}
static char *error(const char *code){return done(result(false,code));}
static uint64_t milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static bool entity(const char *name,char domain[32])
{
  const char *dot=strchr(name,'.');if(!dot||dot==name||!dot[1]||dot-name>=32||strlen(name)>95)return false;
  for(const char *p=name;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||p==dot))return false;
  memcpy(domain,name,dot-name);domain[dot-name]=0;return true;
}
bool glass_assistant_action_valid(const char *action,const cJSON *args)
{
  if(!strcmp(action,"music_stop")||!strcmp(action,"weather_speak"))return keys(args,"");
  if(!strcmp(action,"notify")||!strcmp(action,"speak"))
    return keys(args,",text,")&&string(args,"text")[0]&&glass_chat_utf8(string(args,"text"),512,true);
  char domain[32];if(!entity(string(args,"entity"),domain)||!strcmp(domain,"lock")||!strcmp(domain,"alarm_control_panel"))return false;
  if(!strcmp(action,"home_control")){
    if(!keys(args,",entity,,on,,brightness,")||!cJSON_IsBool(get(args,"on")))return false;
    if(strcmp(domain,"light")&&strcmp(domain,"switch")&&strcmp(domain,"fan")&&strcmp(domain,"input_boolean"))return false;
    return !get(args,"brightness")||(!strcmp(domain,"light")&&cJSON_IsTrue(get(args,"on"))&&number(get(args,"brightness"),1,100));
  }
  if(strcmp(action,"home_service")||!keys(args,",entity,,service,,arguments,"))return false;
  const cJSON *input=get(args,"arguments");if(input&&!cJSON_IsObject(input))return false;
  if(input&&get(input,"entity_id"))return false;
  cJSON *payload=input?cJSON_Duplicate(input,true):cJSON_CreateObject();if(!payload)return false;
  cJSON_AddStringToObject(payload,"entity_id",string(args,"entity"));char *body=cJSON_PrintUnformatted(payload);cJSON_Delete(payload);
  bool valid=body&&hass_action_valid(domain,string(args,"service"),string(args,"entity"),body);free(body);return valid;
}
static char *speak(const char *text)
{
#ifdef GLASS_LOCAL_SPEECH
  uint32_t request=0;int ret=glass_speech_say_request(text,&request);
  if(ret)return error(ret==-EBUSY?"audio_busy":"speech_start_failed");
  cJSON *o=result(true,NULL);cJSON_AddBoolToObject(o,"accepted",true);cJSON_AddNumberToObject(o,"speech_request",request);return done(o);
#else
  (void)text;return error("local_speech_not_built");
#endif
}
static char *weather(const cJSON *args)
{
  if(!keys(args,",refresh,,speak,")||(get(args,"refresh")&&!cJSON_IsBool(get(args,"refresh")))||
    (get(args,"speak")&&!cJSON_IsBool(get(args,"speak"))))return error("invalid_arguments");
  if(cJSON_IsTrue(get(args,"refresh"))){glass_weather_start();glass_weather_refresh();}
  struct glass_weather_snapshot s;glass_weather_get(&s);
  static const char *states[]={"waiting_network","waiting_clock","fetching","ready","failed"};
  cJSON *o=result(true,NULL);cJSON_AddBoolToObject(o,"valid",s.valid);cJSON_AddStringToObject(o,"state",
    (unsigned)s.state<5?states[s.state]:"unknown");
  cJSON_AddBoolToObject(o,"refresh_requested",cJSON_IsTrue(get(args,"refresh")));
  cJSON_AddNumberToObject(o,"service_error",s.error);
  if(!s.valid){cJSON_AddStringToObject(o,"note","天气暂无有效数据；刷新仅表示请求更新，不能编造当前天气。");return done(o);}
  uint64_t now=milliseconds(),age=now>=s.received_ms?now-s.received_ms:0;
  bool stale=age>3600000||s.state!=WEATHER_READY;
  cJSON_AddBoolToObject(o,"stale",stale);cJSON_AddNumberToObject(o,"age_ms",(double)age);
  cJSON_AddStringToObject(o,"location",s.data.location);cJSON_AddStringToObject(o,"condition",s.data.condition);
  cJSON_AddStringToObject(o,"report_time",s.data.report_time);cJSON_AddNumberToObject(o,"temperature_c",s.data.temperature);
  cJSON_AddNumberToObject(o,"humidity_pct",s.data.humidity);cJSON_AddNumberToObject(o,"aqi",s.data.aqi);
  cJSON_AddStringToObject(o,"scope","current_location_observation_only");
  if(cJSON_IsTrue(get(args,"speak"))){
    char text[384];snprintf(text,sizeof(text),"%s天气。%s，%s，气温%d摄氏度，湿度百分之%d。",stale?"缓存":"当前",s.data.location,s.data.condition,s.data.temperature,s.data.humidity);
    char *reply=speak(text);cJSON *speech=reply?glass_agent_json(reply):NULL;free(reply);
    cJSON_AddItemToObject(o,"speech",speech?speech:result(false,"speech_unavailable"));
  }
  return done(o);
}
static char *diagnostics(void)
{
  cJSON *o=result(true,NULL);cJSON_AddNumberToObject(o,"uptime_ms",(double)milliseconds());
  cJSON *network=cJSON_AddObjectToObject(o,"network");
#ifdef CONFIG_SYSTEM_C6_DESKTOP
  struct c6_link_snapshot link={0};struct in_addr ip={0};int ret=c6net_get_link_snapshot(&link);
  cJSON_AddBoolToObject(network,"available",ret==0);cJSON_AddBoolToObject(network,"associated",!ret&&link.associated);
  cJSON_AddBoolToObject(network,"carrier_ready",!ret&&link.carrier_ready);
  cJSON_AddBoolToObject(network,"ipv4_lease",netlib_get_ipv4addr(C6NET_IFNAME,&ip)==0&&ip.s_addr!=0);
#else
  cJSON_AddBoolToObject(network,"available",false);
#endif
  cJSON_AddStringToObject(network,"scope","link_and_lease_not_internet_reachability");
  cJSON *heap=cJSON_AddObjectToObject(o,"heap");
#ifdef __NuttX__
  struct mallinfo mi=mallinfo();cJSON_AddBoolToObject(heap,"available",true);
  cJSON_AddNumberToObject(heap,"allocated_bytes",(double)mi.uordblks);cJSON_AddNumberToObject(heap,"free_bytes",(double)mi.fordblks);
#else
  cJSON_AddBoolToObject(heap,"available",false);
#endif
  cJSON *volumes=cJSON_AddArrayToObject(o,"storage");const char *paths[]={"/data","/sdcard"};
  for(unsigned i=0;i<2;i++){
    cJSON *v=cJSON_CreateObject();cJSON_AddStringToObject(v,"path",paths[i]);struct statfs fs;
    int storage_error=statfs(paths[i],&fs);cJSON_AddBoolToObject(v,"available",storage_error==0);
    if(!storage_error){cJSON_AddNumberToObject(v,"total_bytes",(double)fs.f_blocks*fs.f_bsize);cJSON_AddNumberToObject(v,"free_bytes",(double)fs.f_bavail*fs.f_bsize);}
    cJSON_AddItemToArray(volumes,v);
  }
  int percent;bool charging;glass_status_power_read(&percent,&charging);
  cJSON *battery=cJSON_AddObjectToObject(o,"battery");cJSON_AddBoolToObject(battery,"available",percent>=0);
  if(percent>=0){cJSON_AddNumberToObject(battery,"percent",percent);cJSON_AddBoolToObject(battery,"charging",charging);}
  return done(o);
}

/* An opaque ticket distinguishes assistant ownership from other camera users. */
static pthread_mutex_t camera_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned camera_ticket,camera_request;
static unsigned camera_upload_ticket;
static uint32_t camera_save_job;
static int camera_save_error;
static bool camera_save_cancelled;
static struct qpk_dl_result *camera_save_view;
static uint64_t camera_created_ms;
static bool camera_preview,camera_saved,camera_recognition;
static char camera_path[128];
#ifndef ASSISTANT_PHOTO_ROOT
#define ASSISTANT_PHOTO_ROOT NULL
#endif
static int save_frame(const struct qpk_dl_result *view)
{
  uint16_t *pixels=malloc(QPK_DL_PIXELS*sizeof(*pixels));
  struct qpk_dl_result *copy=malloc(sizeof(*copy));
  if(!pixels||!copy){free(pixels);free(copy);return -ENOMEM;}
  int ret=qpk_dl_copy_preview(view->request,view->frame,pixels,QPK_DL_PIXELS,copy);
  if(!ret){
    struct qpk_photo_input input={.format=QPK_PHOTO_BMP24,.data=pixels,
      .bytes=QPK_DL_PIXELS*2,.width=QPK_DL_WIDTH,.height=QPK_DL_HEIGHT,
      .stride=QPK_DL_WIDTH,.zoom=100,.root_override=ASSISTANT_PHOTO_ROOT};
    ret=qpk_photo_store_submit(QPK_PHOTO_ASSISTANT,&input,&camera_save_job);
  }
  if(!ret){free(camera_save_view);camera_save_view=copy;}else free(copy);
  free(pixels);return ret;
}

static int camera_unique_usb(const cJSON *items,unsigned *device,uint32_t *generation)
{
  const cJSON *entry;unsigned count=0;
  cJSON_ArrayForEach(entry,items)if(!strcmp(string(entry,"source"),"usb")&&cJSON_IsTrue(get(entry,"connected"))&&cJSON_IsTrue(get(entry,"supported"))){
    if(!number(get(entry,"device"),0,UVC_CAMERA_MAX_DEVICES-1)||!number(get(entry,"generation"),1,4294967295.0))return -ENODEV;
    *device=(unsigned)get(entry,"device")->valuedouble;*generation=(uint32_t)get(entry,"generation")->valuedouble;count++;
  }
  return count==1?0:count?-EEXIST:-ENODEV;
}
static char *camera_list(void)
{
  cJSON *o=result(true,NULL),*items=cJSON_AddArrayToObject(o,"items");struct stat st;
  cJSON_AddStringToObject(o,"selection_policy","csi0_then_unique_supported_usb");
  cJSON_AddBoolToObject(o,"cloud_vision_requires_upload",true);
  bool csi=false;
  if(!stat("/dev/video0",&st)&&S_ISCHR(st.st_mode)){
    csi=true;
    cJSON *d=cJSON_CreateObject();cJSON_AddStringToObject(d,"source","csi0");cJSON_AddBoolToObject(d,"capture_requires_preview",true);
    cJSON_AddBoolToObject(d,"default",true);
    cJSON_AddBoolToObject(d,"local_classification",false);cJSON_AddItemToArray(items,d);
  }
  int fd=open("/dev/uvcctl",O_RDONLY);if(fd<0){cJSON_AddNumberToObject(o,"usb_inventory_error",-errno);return done(o);}
  struct uvc_camera_list list={0};int ret=ioctl(fd,UVCIOC_LIST,(unsigned long)&list);int code=errno;close(fd);
  if(ret<0){cJSON_AddNumberToObject(o,"usb_inventory_error",-code);return done(o);}
  unsigned usable=0;for(unsigned i=0;i<list.count&&i<UVC_CAMERA_MAX_DEVICES;i++)if(list.devices[i].connected&&list.devices[i].supported)usable++;
  for(unsigned i=0;i<list.count&&i<UVC_CAMERA_MAX_DEVICES;i++){
    struct uvc_camera_device *d=&list.devices[i];cJSON *entry=cJSON_CreateObject();
    cJSON_AddNumberToObject(entry,"device",d->id);cJSON_AddNumberToObject(entry,"generation",d->generation);
    cJSON_AddStringToObject(entry,"name",d->name);cJSON_AddBoolToObject(entry,"connected",d->connected);
    cJSON_AddBoolToObject(entry,"default",!csi&&usable==1&&d->connected&&d->supported);
    cJSON_AddBoolToObject(entry,"supported",d->supported);cJSON_AddStringToObject(entry,"source","usb");cJSON_AddItemToArray(items,entry);
  }
  return done(o);
}
static char *vision(const cJSON *args,atomic_bool *cancel)
{
  if(!keys(args,",request_id,,question,,upload,")||!number(get(args,"request_id"),1,2147483647)||
     !string(args,"question")[0]||!glass_chat_utf8(string(args,"question"),512,true))return error("invalid_arguments");
  if(!cJSON_IsTrue(get(args,"upload")))return error("image_upload_not_authorized");
  char path[sizeof(camera_path)];
  if(pthread_mutex_trylock(&camera_lock))return error("assistant_camera_busy");
  bool owned=get(args,"request_id")->valuedouble==camera_ticket&&camera_saved&&camera_path[0];
  if(!owned){pthread_mutex_unlock(&camera_lock);return error("completed_camera_receipt_required");}
  if(milliseconds()-camera_created_ms>120000){pthread_mutex_unlock(&camera_lock);return error("camera_image_expired");}
  if(camera_upload_ticket==camera_ticket){pthread_mutex_unlock(&camera_lock);return error("image_upload_already_attempted");}
  camera_upload_ticket=camera_ticket;
  snprintf(path,sizeof(path),"%s",camera_path);pthread_mutex_unlock(&camera_lock);
  return glass_vision_analyze(path,string(args,"question"),cancel);
}
static char *camera(const cJSON *args,atomic_bool *cancel)
{
  const char *op=string(args,"operation");bool start=!strcmp(op,"capture")||!strcmp(op,"recognize");
  const char *allowed=!strcmp(op,"list")?",operation,":start?",operation,,device,,generation,,source,":
    !strcmp(op,"photo_status")||!strcmp(op,"recognition_status")||!strcmp(op,"cancel")?",operation,,request_id,":NULL;
  if(!allowed||!keys(args,allowed))return error("invalid_arguments");
  if(!strcmp(op,"list"))return camera_list();
  if(pthread_mutex_trylock(&camera_lock))return error("assistant_camera_busy");
  char *out=NULL;
  if(start){
    printf("[camera-flow] assistant operation=%s at_ms=%llu\n",op,(unsigned long long)milliseconds());
    if(camera_save_job){
      struct qpk_photo_result saving;
      if(!qpk_photo_store_status(QPK_PHOTO_ASSISTANT,camera_save_job,&saving)&&!saving.done){out=error("camera_busy");goto done;}
    }
    const char *source=string(args,"source");cJSON *selected=NULL;
    if(get(args,"source")){
      if(get(args,"device")||get(args,"generation")||
         (strcmp(source,"default")&&strcmp(source,"usb")&&strcmp(source,"csi0"))){out=error("invalid_arguments");goto done;}
      struct stat st;bool csi=!stat("/dev/video0",&st)&&S_ISCHR(st.st_mode);
      if(!strcmp(source,"csi0")&&!csi){out=error("csi_camera_unavailable");goto done;}
      if(!strcmp(source,"usb")||(!strcmp(source,"default")&&!csi)){
        char *inventory=camera_list();cJSON *list=inventory?glass_agent_json(inventory):NULL;free(inventory);
        unsigned device=0;uint32_t generation=0;int selection=camera_unique_usb(get(list,"items"),&device,&generation);
        if(selection){cJSON_Delete(list);out=error(selection==-EEXIST?"ambiguous_usb_camera":"usb_camera_unavailable");goto done;}
        selected=cJSON_Duplicate(args,true);if(!selected){cJSON_Delete(list);goto done;}
        cJSON_AddNumberToObject(selected,"device",device);cJSON_AddNumberToObject(selected,"generation",generation);
        cJSON_DeleteItemFromObjectCaseSensitive(selected,"source");cJSON_Delete(list);
        pthread_mutex_unlock(&camera_lock);out=camera(selected,cancel);cJSON_Delete(selected);return out;
      }
    }
    bool preview=!get(args,"device")&&!strcmp(op,"capture");
    if(preview&&get(args,"generation")){out=error("invalid_arguments");goto done;}
    if(!preview&&(!number(get(args,"device"),0,UVC_CAMERA_MAX_DEVICES-1)||!number(get(args,"generation"),1,4294967295.0))){out=error("camera_inventory_required");goto done;}
    if(camera_request&&!camera_saved){
      if(camera_preview){out=error("check_previous_photo_status_before_capture");goto done;}
      struct qpk_dl_result s;qpk_dl_status(&s);if(s.request==camera_request&&s.busy){out=error("camera_busy");goto done;}
    }
    unsigned request=0;
    if(preview){
      cJSON *empty=cJSON_CreateObject();char *response=glass_board_ui("camera_capture",empty,cancel);cJSON_Delete(empty);
      cJSON *r=response?glass_agent_json(response):NULL;
      if(!r||!cJSON_IsTrue(get(r,"accepted"))){cJSON_Delete(r);out=response?response:error("camera_unavailable");goto done;}
      request=(unsigned)get(r,"request_id")->valuedouble;cJSON_Delete(r);free(response);
    }else{
      struct qpk_dl_request req={.mode=!strcmp(op,"recognize")?QPK_DL_CLASSIFY:QPK_DL_SNAPSHOT,
        .device=(unsigned)get(args,"device")->valuedouble,.generation=(uint32_t)get(args,"generation")->valuedouble};
      int id=qpk_dl_start(&req);if(id<0){out=error(id==-EBUSY?"camera_busy":"camera_start_failed");goto done;}request=(unsigned)id;
    }
    camera_ticket=(camera_ticket+1)&0x7fffffff;if(!camera_ticket)camera_ticket=1;
    camera_created_ms=milliseconds();
    free(camera_save_view);camera_save_view=NULL;
    camera_request=request;camera_preview=preview;camera_save_job=0;camera_save_error=0;camera_save_cancelled=false;camera_saved=false;camera_path[0]=0;camera_recognition=!strcmp(op,"recognize");
    cJSON *r=result(true,NULL);cJSON_AddBoolToObject(r,"accepted",true);cJSON_AddNumberToObject(r,"request_id",camera_ticket);out=done(r);goto done;
  }
  if(!number(get(args,"request_id"),1,2147483647)||get(args,"request_id")->valuedouble!=camera_ticket||!camera_request){out=error("camera_request_not_owned_or_replaced");goto done;}
  if(camera_preview){
    if(strcmp(op,"photo_status")){out=error("preview_photo_requires_photo_status");goto done;}
    cJSON *query=cJSON_CreateObject();cJSON_AddNumberToObject(query,"request_id",camera_request);
    out=glass_board_ui("camera_photo_status",query,cancel);cJSON_Delete(query);
    cJSON *r=out?glass_agent_json(out):NULL;
    if(r&&cJSON_IsTrue(get(r,"done"))){camera_saved=true;
      if(cJSON_IsTrue(get(r,"ok"))&&!get(r,"photo_save_error")&&string(r,"path")[0])snprintf(camera_path,sizeof(camera_path),"%s",string(r,"path"));}
    if(r)cJSON_AddNumberToObject(r,"request_id",camera_ticket);
    free(out);out=r?done(r):error("photo_status_unavailable");goto done;
  }
  if(!strcmp(op,"cancel")){qpk_dl_cancel_request(camera_request);if(camera_save_job)qpk_photo_store_cancel(QPK_PHOTO_ASSISTANT,camera_save_job);camera_save_cancelled=true;camera_save_error=-ECANCELED;camera_path[0]=0;camera_saved=true;out=done(result(true,NULL));goto done;}
  if(camera_recognition!=!strcmp(op,"recognition_status")){out=error("wrong_camera_status_operation");goto done;}
  if(camera_save_cancelled){
    bool finished;
    if(camera_save_job){struct qpk_photo_result saved;int ret=qpk_photo_store_status(QPK_PHOTO_ASSISTANT,camera_save_job,&saved);finished=ret||saved.done;}
    else{struct qpk_dl_result capture;qpk_dl_status(&capture);finished=capture.request!=camera_request||!capture.busy;}
    cJSON *r=result(false,"camera_cancelled");cJSON_AddNumberToObject(r,"request_id",camera_ticket);
    cJSON_AddBoolToObject(r,"done",finished);cJSON_AddNumberToObject(r,"service_error",-ECANCELED);out=done(r);goto done;
  }
  struct qpk_dl_result *s=malloc(sizeof(*s));if(!s){out=NULL;goto done;}
  if(camera_save_job&&camera_save_view)*s=*camera_save_view;else qpk_dl_status(s);
  if(s->request!=camera_request){free(s);out=error("camera_request_replaced");goto done;}
  cJSON *r=result(s->error==0,s->error?"camera_or_model_failed":NULL);cJSON_AddNumberToObject(r,"request_id",camera_ticket);
  cJSON_AddBoolToObject(r,"done",!s->busy);cJSON_AddNumberToObject(r,"service_error",s->error);
  if(!s->busy&&!s->error&&s->frame){
    if(!camera_saved){
      if(!camera_save_job&&!camera_save_error)camera_save_error=save_frame(s);
      if(camera_save_job){
        struct qpk_photo_result saved;
        int ret=qpk_photo_store_status(QPK_PHOTO_ASSISTANT,camera_save_job,&saved);
        if(ret)camera_save_error=ret;
        else if(saved.done){
          camera_saved=true;camera_save_error=saved.error;
          if(!saved.error)snprintf(camera_path,sizeof(camera_path),"%s",saved.path);
        }
      }
      if(camera_save_error)camera_saved=true;
    }
    if(!camera_saved)cJSON_ReplaceItemInObjectCaseSensitive(r,"done",cJSON_CreateFalse());
    if(camera_save_error){
      cJSON_AddNumberToObject(r,"photo_save_error",camera_save_error);
      cJSON_ReplaceItemInObjectCaseSensitive(r,"ok",cJSON_CreateFalse());
    }
    if(camera_saved&&camera_path[0])cJSON_AddStringToObject(r,"path",camera_path);
    cJSON_AddStringToObject(r,"scope","local_model_classification_not_general_vision");
    if(camera_recognition){cJSON *items=cJSON_AddArrayToObject(r,"items");
      for(unsigned i=0;i<s->count&&i<QPK_DL_MAX_RESULTS;i++){
        cJSON *entry=cJSON_CreateObject();cJSON_AddStringToObject(entry,"label",s->items[i].label);cJSON_AddNumberToObject(entry,"score",s->items[i].score);cJSON_AddItemToArray(items,entry);
      }
    }
  }
  free(s);out=done(r);
done:
  pthread_mutex_unlock(&camera_lock);return out;
}
char *glass_assistant_action(const char *action,const cJSON *args,atomic_bool *cancel)
{
  if(cancel&&atomic_load(cancel))return error("cancelled");
  if(!glass_assistant_action_valid(action,args))return error("invalid_action_arguments");
  if(!strcmp(action,"notify")){int ret=glass_assistant_notice(string(args,"text"));return ret?error("notification_memory_unavailable"):done(result(true,NULL));}
  if(!strcmp(action,"speak"))return speak(string(args,"text"));
  if(!strcmp(action,"weather_speak")){
    cJSON *a=cJSON_CreateObject();cJSON_AddBoolToObject(a,"speak",true);char *out=weather(a);cJSON_Delete(a);
    cJSON *r=out?glass_agent_json(out):NULL,*s=r?(cJSON *)get(r,"speech"):NULL;
    char *answer=s?cJSON_PrintUnformatted(s):error("weather_unavailable");cJSON_Delete(r);free(out);return answer;
  }
  if(!strcmp(action,"music_stop")){glass_music_stop();struct music_now_playing *s=malloc(sizeof(*s));if(!s)return NULL;
    glass_music_now_playing(s);cJSON *r=result(true,NULL);cJSON_AddBoolToObject(r,"verified",s->state==MUSIC_STOPPED);free(s);return done(r);}
  /* Re-enter the real home bridge, including permission and readback checks. */
  struct glass_agent *agent=glass_agent_create(cancel,AGENT_MODE_ASSISTANT);if(!agent)return error("agent_unavailable");
  char *out=NULL;int ret=glass_agent_call(agent,action,args,&out);glass_agent_destroy(agent);
  return ret>0&&out?out:error("home_service_unavailable");
}
char *glass_assistant_platform(const char *tool,const cJSON *args,atomic_bool *cancel)
{
  if(cancel&&atomic_load(cancel))return error("cancelled");
  if(!strcmp(tool,"assistant_weather"))return weather(args);
  if(!strcmp(tool,"assistant_speak"))return glass_assistant_action_valid("speak",args)?speak(string(args,"text")):error("invalid_arguments");
  if(!strcmp(tool,"assistant_diagnostics"))return keys(args,"")?diagnostics():error("invalid_arguments");
  if(!strcmp(tool,"assistant_camera"))return camera(args,cancel);
  if(!strcmp(tool,"assistant_vision"))return vision(args,cancel);
  if(!strcmp(tool,"assistant_observe")){
    const char *question=string(args,"question");
    if(!keys(args,",question,,upload,")||!question[0]||!glass_chat_utf8(question,512,true)||
       !cJSON_IsTrue(get(args,"upload")))return error("invalid_arguments_or_upload_not_requested");
    return glass_chat_observe_tool(question,cancel);
  }
  if(!strcmp(tool,"assistant_status")){
    cJSON *r=result(true,NULL);
#ifdef GLASS_LOCAL_SPEECH
    struct speech_snapshot s;glass_speech_get(&s);cJSON *speech=cJSON_AddObjectToObject(r,"speech");
    cJSON_AddNumberToObject(speech,"request_id",s.request);cJSON_AddBoolToObject(speech,"active",s.active);
    cJSON_AddNumberToObject(speech,"phase",s.phase);cJSON_AddNumberToObject(speech,"service_error",s.error);
#endif
    pthread_mutex_lock(&camera_lock);cJSON_AddNumberToObject(r,"camera_request",camera_ticket);pthread_mutex_unlock(&camera_lock);return done(r);
  }
  return error("unknown_assistant_tool");
}
