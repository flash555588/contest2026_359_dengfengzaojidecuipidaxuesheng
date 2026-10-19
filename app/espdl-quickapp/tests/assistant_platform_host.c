/* SPDX-License-Identifier: Apache-2.0 */
/* Production platform bridge and real ESP-DL worker; device boundaries mocked. */
#include <assert.h>
#include "../overlay/apps/system/desktop/glass_assistant_platform.c"
static struct glass_weather_snapshot weather_fixture;
static unsigned refreshes,spoken,backend_opens,backend_closes,capture_opens,capture_closes;
static atomic_bool hold_capture;
static bool audio_busy,home_permission=true;
static char last_spoken[512];
static struct speech_snapshot speech_fixture;
void glass_weather_get(struct glass_weather_snapshot *out){*out=weather_fixture;}
void glass_weather_start(void){}
void glass_weather_refresh(void){refreshes++;}
int glass_speech_say_request(const char *text,uint32_t *request)
{if(audio_busy)return -EBUSY;spoken++;snprintf(last_spoken,sizeof(last_spoken),"%s",text);*request=++speech_fixture.request;speech_fixture.active=true;return 0;}
void glass_speech_get(struct speech_snapshot *out){*out=speech_fixture;}
void glass_status_power_read(int *percent,bool *charging){*percent=-1;*charging=false;}
void glass_music_stop(void){}
void glass_music_now_playing(struct music_now_playing *out){memset(out,0,sizeof(*out));out->state=MUSIC_STOPPED;}
int glass_assistant_notice(const char *text){(void)text;return 0;}
struct glass_agent *glass_agent_create(atomic_bool *cancel,enum glass_agent_mode mode)
{(void)cancel;assert(mode==AGENT_MODE_ASSISTANT);return (struct glass_agent *)malloc(1);}
void glass_agent_destroy(struct glass_agent *a){free(a);}
int glass_agent_call(struct glass_agent *a,const char *tool,const cJSON *args,char **out)
{assert(a&&!strcmp(tool,"home_control")&&get(args,"entity"));*out=strdup(home_permission?"{\"ok\":true,\"verified\":true}":"{\"ok\":false,\"error\":\"home_control_disabled\"}");return 1;}
char *glass_board_ui(const char *op,const cJSON *args,atomic_bool *cancel)
{
  (void)cancel;
  if(!strcmp(op,"camera_capture"))return strdup("{\"ok\":true,\"accepted\":true,\"request_id\":77}");
  assert(!strcmp(op,"camera_photo_status")&&get(args,"request_id")->valueint==77);
  return strdup("{\"ok\":true,\"done\":true,\"status\":2,\"path\":\"/data/photos/camera-test.bmp\"}");
}
int qpk_dl_backend_open(enum qpk_dl_mode mode,void **out)
{assert(mode==QPK_DL_CLASSIFY);backend_opens++;*out=malloc(1);return *out?0:-ENOMEM;}
int qpk_dl_backend_run(void *context,const uint16_t *pixels,struct qpk_dl_result *out)
{assert(context&&pixels);out->count=1;strcpy(out->items[0].label,"cup");out->items[0].score=0.75f;return 0;}
void qpk_dl_backend_close(void *context){if(context){backend_closes++;free(context);}}
int qpk_dl_backend_verify(void *context){(void)context;return 0;}
int qpk_dl_capture_open(const struct qpk_dl_request *request,void **context)
{assert(request->device==0&&request->generation==1);capture_opens++;*context=malloc(1);return *context?0:-ENOMEM;}
int qpk_dl_capture_next(void *context,uint16_t *pixels)
{
  assert(context);while(atomic_load(&hold_capture)){if(qpk_dl_should_cancel())return -ECANCELED;usleep(1000);}
  for(unsigned i=0;i<QPK_DL_PIXELS;i++)pixels[i]=(i%2)?0xf800:0x001f;return 0;
}
void qpk_dl_capture_close(void *context){if(context){capture_closes++;free(context);}}
void qpk_dl_track_update(struct qpk_dl_tracker *tracker,struct qpk_dl_result *r){(void)tracker;(void)r;}
static cJSON *invoke(const char *tool,const char *json,bool failed)
{
  cJSON *args=cJSON_Parse(json);assert(args);atomic_bool cancel=false;
  char *text=glass_assistant_platform(tool,args,&cancel);cJSON_Delete(args);assert(text);cJSON *r=cJSON_Parse(text);free(text);assert(r);
  if((get(r,"error")!=NULL)!=failed){char *s=cJSON_PrintUnformatted(r);fprintf(stderr,"%s => %s\n",json,s);free(s);abort();}return r;
}
static unsigned start(const char *op)
{
  char json[128];snprintf(json,sizeof(json),"{\"operation\":\"%s\",\"device\":0,\"generation\":1}",op);
  cJSON *r=invoke("assistant_camera",json,false);assert(cJSON_IsTrue(get(r,"accepted")));
  unsigned id=(unsigned)get(r,"request_id")->valuedouble;cJSON_Delete(r);return id;
}
static cJSON *poll(unsigned id,const char *operation)
{
  for(unsigned n=0;n<3000;n++){
    char json[128];snprintf(json,sizeof(json),"{\"operation\":\"%s\",\"request_id\":%u}",operation,id);
    cJSON *r=invoke("assistant_camera",json,false);
    if(cJSON_IsTrue(get(r,"done")))return r;cJSON_Delete(r);usleep(1000);
  }abort();
}
int main(void)
{
  cJSON *r=invoke("assistant_weather","{}",false);assert(cJSON_IsFalse(get(r,"valid"))&&!get(r,"temperature_c"));cJSON_Delete(r);
  weather_fixture.valid=true;weather_fixture.state=WEATHER_READY;weather_fixture.received_ms=milliseconds();
  strcpy(weather_fixture.data.location,"杭州");strcpy(weather_fixture.data.condition,"晴");strcpy(weather_fixture.data.report_time,"2026-10-19 12:00");weather_fixture.data.temperature=25;weather_fixture.data.humidity=50;
  r=invoke("assistant_weather","{\"refresh\":true,\"speak\":true}",false);assert(refreshes==1&&spoken==1&&!cJSON_IsTrue(get(r,"stale"))&&get(r,"temperature_c")->valueint==25);cJSON_Delete(r);
  weather_fixture.state=WEATHER_FAILED;r=invoke("assistant_weather","{\"speak\":true}",false);assert(cJSON_IsTrue(get(r,"stale"))&&strstr(last_spoken,"缓存"));cJSON_Delete(r);
  audio_busy=true;r=invoke("assistant_weather","{\"speak\":true}",false);assert(get(get(r,"speech"),"error"));cJSON_Delete(r);
  cJSON_Delete(invoke("assistant_speak","{\"text\":\"你好\"}",true));audio_busy=false;
  cJSON_Delete(invoke("assistant_weather","{\"refresh\":1}",true));cJSON_Delete(invoke("assistant_weather","{\"city\":\"北京\"}",true));
  r=invoke("assistant_speak","{\"text\":\"你好\"}",false);unsigned speech_id=(unsigned)get(r,"speech_request")->valuedouble;cJSON_Delete(r);
  r=invoke("assistant_status","{}",false);assert(get(get(r,"speech"),"request_id")->valuedouble==speech_id);cJSON_Delete(r);
  r=invoke("assistant_diagnostics","{}",false);assert(cJSON_IsFalse(get(get(r,"battery"),"available"))&&cJSON_IsFalse(get(get(r,"heap"),"available")));cJSON_Delete(r);
  puts("ASSISTANT_PLATFORM_WEATHER_OK: real snapshot formatting, freshness, missing data, refresh receipt, spoken cache labels, busy audio, speech identity and unavailable diagnostics");

  cJSON *args=cJSON_Parse("{\"entity\":\"light.desk\",\"service\":\"turn_on\",\"arguments\":{\"brightness_pct\":30,\"rgb_color\":[255,192,203]}}");
  assert(glass_assistant_action_valid("home_service",args));cJSON_Delete(args);
  const char *invalid[]={"{\"entity\":\"light.desk\",\"service\":\"turn_on\",\"arguments\":{\"entity_id\":\"light.other\"}}",
    "{\"entity\":\"lock.front\",\"service\":\"unlock\"}","{\"entity\":\"light.desk\",\"service\":\"turn_on\",\"arguments\":{\"rgb_color\":[256,0,0]}}"};
  for(unsigned i=0;i<3;i++){args=cJSON_Parse(invalid[i]);assert(!glass_assistant_action_valid("home_service",args));cJSON_Delete(args);}
  args=cJSON_Parse("{\"entity\":\"light.desk\",\"on\":false}");atomic_bool cancel=false;
  home_permission=false;char *reply=glass_assistant_action("home_control",args,&cancel);assert(strstr(reply,"disabled"));free(reply);cJSON_Delete(args);
  puts("ASSISTANT_ACTIONS_OK: shared HA schema, RGB bounds, extra-target/lock rejection and fresh execution-time permission boundary");

  unsigned id=start("capture");r=poll(id,"photo_status");assert(get(r,"path")&&!get(r,"photo_save_error")&&backend_opens==0);
  char path[128];snprintf(path,sizeof(path),"%s",string(r,"path"));cJSON_Delete(r);
  FILE *file=fopen(path,"rb");assert(file);unsigned char header[54];assert(fread(header,1,54,file)==54&&header[0]=='B'&&header[1]=='M'&&header[28]==24);fclose(file);
  r=poll(id,"photo_status");assert(!strcmp(path,string(r,"path")));cJSON_Delete(r);
  unsigned old=id;id=start("recognize");r=poll(id,"recognition_status");
  assert(backend_opens==1&&backend_closes==1&&cJSON_GetArraySize(get(r,"items"))==1&&get(r,"path"));cJSON_Delete(r);
  char json[128];snprintf(json,sizeof(json),"{\"operation\":\"photo_status\",\"request_id\":%u}",old);cJSON_Delete(invoke("assistant_camera",json,true));
  atomic_store(&hold_capture,true);id=start("recognize");snprintf(json,sizeof(json),"{\"operation\":\"cancel\",\"request_id\":%u}",id);
  cJSON_Delete(invoke("assistant_camera",json,false));
  struct qpk_dl_result view;for(unsigned n=0;n<3000;n++){qpk_dl_status(&view);if(!view.busy)break;usleep(1000);}assert(!view.busy&&view.error==-ECANCELED);atomic_store(&hold_capture,false);
  r=invoke("assistant_camera","{\"operation\":\"capture\"}",false);id=(unsigned)get(r,"request_id")->valuedouble;cJSON_Delete(r);
  r=poll(id,"photo_status");assert(!strcmp(string(r,"path"),"/data/photos/camera-test.bmp"));cJSON_Delete(r);
  cJSON_Delete(invoke("assistant_camera","{\"operation\":\"recognize\"}",true));
  qpk_dl_cancel();assert(backend_opens==backend_closes&&capture_opens==capture_closes);
  puts("ASSISTANT_PLATFORM_CAMERA_OK: real snapshot/classifier workers, no model for plain capture, saved BMP, stable photo receipt, recognition results, cancellation, ownership and preview receipt mapping");
  return 0;
}
