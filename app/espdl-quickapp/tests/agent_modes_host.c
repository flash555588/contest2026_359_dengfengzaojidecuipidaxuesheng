/* SPDX-License-Identifier: Apache-2.0 */
/* Real worker + store; deterministic backend, no network or LVGL. */
#include "glass_chat_backend.h"
#include "glass_vision.h"
#include "qpk_espdl.h"
#include "glass_camera_observation.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static atomic_bool hold, entered, fail_next;
static enum glass_agent_mode request_mode;
static char request_context[CHAT_CONTEXT_BYTES*2];
static atomic_bool capture_hold,capture_entered,camera_closed,local_busy;
static unsigned camera_uploads;
static unsigned recording_saves;
static bool recording_saved,recording_fail;
int glass_camera_recording_check(void){return 0;}
int glass_camera_recording_save(uint32_t id,const char *question,const struct qpk_camera_clip *clip,
 atomic_bool *cancel,char path[128])
{
 assert(id&&question[0]&&clip->count==3&&atomic_load(&camera_closed));
 if(atomic_load(cancel))return -ECANCELED;
 if(recording_fail)return -ENOSPC;
 recording_saved=true;recording_saves++;snprintf(path,128,"/sdcard/espclaw-recordings/record-%u-0/record.json",id);return 0;
}
static uint32_t persisted_last_id;
static unsigned observation_saves;
static bool observation_fail;
static bool long_answer;
void qpk_dl_status(struct qpk_dl_result *out){memset(out,0,sizeof(*out));out->busy=atomic_load(&local_busy);}
int qpk_camera_clip_capture(struct qpk_camera_clip *clip,atomic_bool *cancel)
{
  recording_saved=false;atomic_store(&camera_closed,false);atomic_store(&capture_entered,true);
  /* The queue must not persist a waiting upload before capture closes. */
  struct chat_snapshot *disk=calloc(1,sizeof(*disk));assert(disk&&!glass_chat_store_load(disk));
  assert(disk->count&&disk->turns[disk->count-1].id==persisted_last_id);
  free(disk);
  while(atomic_load(&capture_hold)&&!atomic_load(cancel))usleep(1000);
  clip->count=3;clip->duration_ms=5000;atomic_store(&camera_closed,true);
  return atomic_load(cancel)?-ECANCELED:0;
}
void qpk_camera_clip_free(struct qpk_camera_clip *clip){memset(clip,0,sizeof(*clip));}
char *glass_vision_analyze_clip(const struct qpk_camera_clip *clip,const char *question,atomic_bool *cancel)
{
  assert(recording_saved&&atomic_load(&camera_closed)&&clip->count==3&&question[0]&&!atomic_load(cancel));camera_uploads++;
  return strdup("{\"ok\":true,\"scope\":\"cloud_temporal_samples\",\"answer\":\"camera samples answer\"}");
}
char *glass_vision_analyze_clip_take(struct qpk_camera_clip *clip,const char *question,atomic_bool *cancel)
{return glass_vision_analyze_clip(clip,question,cancel);}
int glass_camera_observation_save(uint32_t id,enum glass_agent_mode mode,
 const char *question,const char *answer,const struct qpk_camera_clip *clip,
 atomic_bool *cancel,char path[128])
{
 assert(id&&glass_agent_mode_valid(mode)&&question[0]&&answer[0]&&!atomic_load(cancel));
 if(clip)assert(atomic_load(&camera_closed)&&clip->count==3);
 if(observation_fail){path[0]=0;return -ENOSPC;}
 observation_saves++;snprintf(path,128,"/sdcard/espclaw-observations/camera-%u-0.json",id);return 0;
}
int glass_chat_load_settings(struct portal_ai_config *out)
{memset(out,0,sizeof(*out));strcpy(out->model,"test");return 0;}
enum chat_error glass_chat_request(const struct portal_ai_config *settings,
  const char *context,const char *prompt,uint32_t id,enum glass_agent_mode mode,
  atomic_bool *cancel,char **reply)
{
  (void)settings;(void)id;request_mode=mode;
  snprintf(request_context,sizeof(request_context),"%s",context);
  atomic_store(&entered,true);
  while(atomic_load(&hold)&&!atomic_load(cancel))usleep(1000);
  if(atomic_exchange(&fail_next,false))return CHAT_ERROR_NETWORK;
  if(!strcmp(prompt,"agent-look")){
    char *wire=glass_chat_observe_tool("what is on the desk?",cancel);
    assert(wire&&strstr(wire,"camera samples answer"));free(wire);
    wire=glass_chat_observe_tool("do not replay",cancel);
    assert(wire&&strstr(wire,"already_attempted_no_retry"));free(wire);
    *reply=strdup("continued answer after camera tool result");return CHAT_ERROR_NONE;
  }
  if(long_answer){*reply=malloc(CHAT_REPLY_BYTES+100);assert(*reply);memset(*reply,'a',CHAT_REPLY_BYTES+99);(*reply)[CHAT_REPLY_BYTES+99]=0;}
  else *reply=strdup(prompt);return CHAT_ERROR_NONE;
}
static struct chat_snapshot state;
static void idle(void)
{
  for(unsigned i=0;i<5000;i++){
    glass_chat_get(&state);if(state.loaded&&state.phase==CHAT_IDLE)return;usleep(1000);
  }
  assert(!"worker did not become idle");
}
static void send_mode(const char *text,enum glass_agent_mode mode)
{
  assert(glass_chat_send_mode(text,mode)==0);idle();
  assert(state.turns[state.count-1].mode==mode&&request_mode==mode);
}
int main(void)
{
  mkdir(CHAT_DATA_ROOT,0700);mkdir(CHAT_DATA_ROOT "/chat",0700);
  unlink(CHAT_DATA_ROOT "/chat/history.json");unlink(CHAT_DATA_ROOT "/chat/history.tmp");
  /* Previous firmware wrote no mode field: retain it under creation. */
  FILE *f=fopen(CHAT_DATA_ROOT "/chat/history.json","w");assert(f);
  fputs("{\"version\":1,\"turns\":[{\"id\":1,\"state\":1,\"error\":0,\"prompt\":\"legacy-app\",\"reply\":\"legacy-draft\"}]}",f);fclose(f);
  assert(glass_chat_start()==0);idle();assert(state.count==1&&state.turns[0].mode==AGENT_MODE_CREATE);
  send_mode("casual-talk",AGENT_MODE_CHAT);assert(!strstr(request_context,"legacy"));
  send_mode("assistant-query",AGENT_MODE_ASSISTANT);assert(!strstr(request_context,"casual")&&!strstr(request_context,"legacy"));
  char long_prompt[CHAT_PROMPT_BYTES+1]="";
  for(unsigned i=0;i<50;i++)strcat(long_prompt,"这是完整的长句，");
  strcat(long_prompt,"最后只查询床头灯状态，不要开灯，不要调亮度，也不要改颜色。");
  /* Test the real queue/store/backend path without altering fixture counts. */
  send_mode(long_prompt,AGENT_MODE_ASSISTANT);
  assert(!strcmp(state.turns[state.count-1].prompt,long_prompt));
  assert(!strcmp(state.turns[state.count-1].reply,long_prompt));
  assert(!glass_chat_clear_mode(AGENT_MODE_ASSISTANT));idle();
  send_mode("assistant-query",AGENT_MODE_ASSISTANT);
  send_mode("continue-chat",AGENT_MODE_CHAT);
  assert(strstr(request_context,"casual-talk")&&!strstr(request_context,"assistant-query")&&!strstr(request_context,"legacy"));
  send_mode("continue-app",AGENT_MODE_CREATE);
  assert(strstr(request_context,"legacy-draft")&&!strstr(request_context,"casual-talk")&&!strstr(request_context,"assistant-query"));
  struct chat_snapshot *loaded=calloc(1,sizeof(*loaded));assert(loaded);
  assert(!glass_chat_store_load(loaded)&&loaded->count==5);
  assert(loaded->turns[1].mode==AGENT_MODE_CHAT&&loaded->turns[2].mode==AGENT_MODE_ASSISTANT);free(loaded);
  struct chat_snapshot *filtered=calloc(1,sizeof(*filtered));assert(filtered);
  assert(glass_chat_get_mode(filtered,AGENT_MODE_CHAT)&&filtered->count==2);
  assert(!glass_chat_get_mode(filtered,AGENT_MODE_CHAT));
  assert(glass_chat_get_mode(filtered,AGENT_MODE_ASSISTANT)&&filtered->count==1);
  assert(glass_chat_get_mode(filtered,AGENT_MODE_CHAT)&&filtered->count==2);free(filtered);
  assert(!glass_chat_clear_mode(AGENT_MODE_CHAT));idle();assert(state.count==3);
  for(unsigned i=0;i<state.count;i++)assert(state.turns[i].mode!=AGENT_MODE_CHAT);
  atomic_store(&fail_next,true);send_mode("retry-assistant",AGENT_MODE_ASSISTANT);
  assert(state.turns[state.count-1].state==CHAT_FAILED);
  assert(glass_chat_retry_mode(AGENT_MODE_CHAT)==-EINVAL);
  assert(!glass_chat_retry_mode(AGENT_MODE_ASSISTANT));idle();
  assert(request_mode==AGENT_MODE_ASSISTANT&&state.turns[state.count-1].state==CHAT_DONE);
  atomic_store(&fail_next,true);send_mode("interleaved-retry",AGENT_MODE_CHAT);
  assert(state.turns[state.count-1].state==CHAT_FAILED);
  send_mode("other-mode-completed",AGENT_MODE_ASSISTANT);
  assert(!glass_chat_retry_mode(AGENT_MODE_CHAT));idle();
  assert(request_mode==AGENT_MODE_CHAT&&!strcmp(state.turns[state.count-1].prompt,"interleaved-retry"));
  assert(!strstr(request_context,"other-mode-completed"));
  for(unsigned i=1;i<state.count;i++)assert(state.turns[i].id>state.turns[i-1].id);
  atomic_store(&hold,true);atomic_store(&entered,false);
  assert(!glass_chat_send_mode("frozen-chat",AGENT_MODE_CHAT));
  for(unsigned i=0;i<5000&&!atomic_load(&entered);i++)usleep(1000);
  assert(atomic_load(&entered)&&request_mode==AGENT_MODE_CHAT);
  assert(glass_chat_send_mode("cannot-switch-request",AGENT_MODE_CREATE)==-EBUSY);
  assert(glass_chat_clear_mode(AGENT_MODE_ASSISTANT)==-EBUSY);
  assert(!glass_chat_clear_mode(AGENT_MODE_CHAT));atomic_store(&hold,false);idle();
  for(unsigned i=0;i<state.count;i++)assert(state.turns[i].mode!=AGENT_MODE_CHAT);
  assert(glass_chat_send_mode("invalid",AGENT_MODE_COUNT)==-EINVAL);
  assert(glass_chat_clear_mode(AGENT_MODE_COUNT)==-EINVAL);
  /* Compact result views are scoped to one turn/mode and preserve trusted
   * receipt confirmation, UTF-8 boundaries and exact continuation offsets. */
  atomic_store(&hold,true);atomic_store(&entered,false);
  char chinese[700]="";for(unsigned i=0;i<35;i++)strcat(chinese,"测试结果。");
  assert(!glass_chat_send_mode(chinese,AGENT_MODE_ASSISTANT));
  while(!atomic_load(&entered))usleep(1000);
  struct chat_result_snapshot result;assert(glass_chat_result_get(&result,0,AGENT_MODE_ASSISTANT,0));
  uint32_t result_id=result.turn_id;assert(result.state==CHAT_WAITING);
  assert(!glass_chat_result_get(&result,result_id,AGENT_MODE_CHAT,0));
  struct claw_stream_event event={.kind=CLAW_STREAM_CARD,.text="{\"entity\":\"light.test\",\"kind\":\"light\",\"name\":\"主灯\",\"state\":\"已开启\",\"level\":30,\"on\":true,\"action\":true,\"verified\":true}"};
  glass_chat_stream(result_id,&event);
  assert(glass_chat_result_get(&result,result_id,AGENT_MODE_ASSISTANT,0)&&result.card_count==1&&result.cards[0].verified);
  event.text="{\"entity\":\"light.test\",\"kind\":\"light\",\"name\":\"主灯\",\"state\":\"结果未确认\",\"level\":-1,\"on\":false,\"action\":true,\"verified\":false}";
  glass_chat_stream(result_id,&event);atomic_store(&hold,false);idle();
  assert(glass_chat_result_get(&result,result_id,AGENT_MODE_ASSISTANT,0)&&!result.cards[0].verified&&result.cards[0].action);
  char joined[800]="";size_t offset=0;
  do{assert(glass_chat_result_get(&result,result_id,AGENT_MODE_ASSISTANT,offset));assert(glass_chat_utf8(result.reply,240,false));strcat(joined,result.reply);assert(result.next_offset>offset);offset=result.next_offset;}while(result.more);
  assert(!strcmp(joined,chinese));
  assert(!glass_chat_result_get(&result,result_id+999,AGENT_MODE_ASSISTANT,0));
  /* Result cards persist per turn and survive a reload; invalid card fields
   * reject the stored file instead of rendering damaged history. */
  struct chat_snapshot *withcards=calloc(1,sizeof(*withcards));assert(withcards);
  assert(!glass_chat_store_load(withcards)&&withcards->count==state.count);
  struct chat_turn *last=&withcards->turns[withcards->count-1];
  last->card_count=2;
  strcpy(last->cards[0].entity,"light.living_room");
  snprintf(last->cards[0].kind,sizeof(last->cards[0].kind),"light");
  snprintf(last->cards[0].name,sizeof(last->cards[0].name),"客厅主灯");
  snprintf(last->cards[0].state,sizeof(last->cards[0].state),"已开启");
  last->cards[0].level=80;last->cards[0].on=true;
  last->cards[0].action=last->cards[0].verified=true;
  snprintf(last->cards[1].kind,sizeof(last->cards[1].kind),"sensor");
  snprintf(last->cards[1].name,sizeof(last->cards[1].name),"客厅温度");
  snprintf(last->cards[1].state,sizeof(last->cards[1].state),"23.5 °C");
  last->cards[1].level=-1;last->cards[1].on=false;
  assert(!glass_chat_store_save(withcards));
  struct chat_snapshot *reloaded=calloc(1,sizeof(*reloaded));assert(reloaded);
  assert(!glass_chat_store_load(reloaded)&&reloaded->count==withcards->count);
  const struct chat_turn *saved=&reloaded->turns[reloaded->count-1];
  assert(!strcmp(saved->cards[0].entity,"light.living_room"));
  assert(saved->cards[0].action&&saved->cards[0].verified&&!saved->cards[1].action);
  assert(saved->card_count==2&&!strcmp(saved->cards[0].name,"客厅主灯")&&saved->cards[0].level==80&&saved->cards[0].on&&
         !strcmp(saved->cards[1].kind,"sensor")&&!strcmp(saved->cards[1].state,"23.5 °C")&&!saved->cards[1].on);
  free(withcards);free(reloaded);
  /* Explicit camera upload is separate from local inference and text tools.
   * Capture closes before upload, cancellation is final, retries do not
   * silently capture/upload again, and the origin survives persistence. */
  assert(glass_chat_send_camera("camera",AGENT_MODE_CREATE)==-EACCES);
  struct chat_snapshot *legacy_camera=calloc(1,sizeof(*legacy_camera));assert(legacy_camera);
  assert(!glass_chat_store_load(legacy_camera)&&legacy_camera->count);
  persisted_last_id=legacy_camera->turns[legacy_camera->count-1].id;
  legacy_camera->turns[legacy_camera->count-1].camera_samples=true;
  assert(!glass_chat_store_save(legacy_camera));
  struct chat_snapshot *legacy_check=calloc(1,sizeof(*legacy_check));assert(legacy_check);
  assert(!glass_chat_store_load(legacy_check)&&legacy_check->turns[legacy_check->count-1].camera_samples);
  free(legacy_check);legacy_camera->turns[legacy_camera->count-1].camera_samples=false;
  assert(!glass_chat_store_save(legacy_camera));free(legacy_camera);
  atomic_store(&local_busy,true);
  assert(!glass_chat_send_camera("camera",AGENT_MODE_CHAT));idle();
  assert(state.turns[state.count-1].error==CHAT_ERROR_CAMERA&&!camera_uploads);
  struct chat_snapshot *failed_camera=calloc(1,sizeof(*failed_camera));assert(failed_camera);
  assert(!glass_chat_store_load(failed_camera)&&failed_camera->turns[failed_camera->count-1].id==persisted_last_id);free(failed_camera);
  f=fopen(CHAT_DATA_ROOT "/chat/history.json","r");assert(f);char stored_camera[65536];
  size_t stored_bytes=fread(stored_camera,1,sizeof(stored_camera)-1,f);fclose(f);stored_camera[stored_bytes]=0;
  cJSON *compatible=cJSON_Parse(stored_camera);assert(compatible);const cJSON *item;
  cJSON_ArrayForEach(item,cJSON_GetObjectItemCaseSensitive(compatible,"turns")){
    assert(cJSON_GetObjectItemCaseSensitive(item,"error")->valueint<=CHAT_ERROR_STREAM);
  }cJSON_Delete(compatible);
  assert(glass_chat_retry_mode(AGENT_MODE_CHAT)==-EACCES);
  assert(!glass_chat_clear_mode(AGENT_MODE_CHAT));idle();atomic_store(&local_busy,false);
  atomic_store(&capture_hold,true);atomic_store(&capture_entered,false);
  assert(!glass_chat_send_camera("camera",AGENT_MODE_CHAT));
  while(!atomic_load(&capture_entered))usleep(1000);
  assert(glass_chat_send_mode("busy",AGENT_MODE_CHAT)==-EBUSY);
  glass_chat_cancel();idle();assert(!camera_uploads&&atomic_load(&camera_closed));
  assert(state.turns[state.count-1].state==CHAT_CANCELLED&&state.turns[state.count-1].camera_samples);
  assert(glass_chat_retry_mode(AGENT_MODE_CHAT)==-EACCES);
  assert(!glass_chat_clear_mode(AGENT_MODE_CHAT));idle();
  atomic_store(&capture_hold,false);assert(!glass_chat_send_camera("camera",AGENT_MODE_CHAT));idle();
  assert(camera_uploads==1&&state.turns[state.count-1].state==CHAT_DONE&&state.turns[state.count-1].camera_samples);
  assert(!strcmp(state.turns[state.count-1].reply,"camera samples answer"));
  struct chat_snapshot *camera_disk=calloc(1,sizeof(*camera_disk));assert(camera_disk);
  assert(!glass_chat_store_load(camera_disk)&&camera_disk->turns[camera_disk->count-1].id==persisted_last_id);
  assert(!camera_disk->turns[camera_disk->count-1].session_history);
  assert(observation_saves==1&&state.turns[state.count-1].camera_observation[0]&&state.turns[state.count-1].session_history);
  uint32_t new_camera_id=state.turns[state.count-1].id;
  send_mode("follow-up-to-camera",AGENT_MODE_ASSISTANT);
  assert(!glass_chat_store_load(camera_disk));
  for(unsigned i=0;i<camera_disk->count;i++)assert(camera_disk->turns[i].id!=new_camera_id);
  uint32_t before_bot=camera_disk->turns[camera_disk->count-1].id;
  assert(!glass_chat_send_bot("voice-bot",AGENT_MODE_ASSISTANT));idle();
  assert(state.turns[state.count-1].state==CHAT_DONE&&state.turns[state.count-1].session_history&&observation_saves==2);
  uint32_t bot_id=state.turns[state.count-1].id;
  assert(!glass_chat_store_load(camera_disk)&&camera_disk->turns[camera_disk->count-1].id==before_bot);
  atomic_store(&hold,true);atomic_store(&entered,false);
  assert(!glass_chat_send_bot("cancel-voice",AGENT_MODE_ASSISTANT));
  while(!atomic_load(&entered))usleep(1000);
  assert(!glass_chat_store_load(camera_disk)&&camera_disk->turns[camera_disk->count-1].id==before_bot);
  glass_chat_cancel();idle();atomic_store(&hold,false);
  assert(state.turns[state.count-1].state==CHAT_CANCELLED&&observation_saves==2);
  observation_fail=true;assert(!glass_chat_retry_mode(AGENT_MODE_ASSISTANT));idle();
  assert(state.turns[state.count-1].state==CHAT_DONE&&state.turns[state.count-1].session_history);
  assert(state.turns[state.count-1].camera_save_error==-ENOSPC&&!state.turns[state.count-1].camera_observation[0]);
  assert(!glass_chat_store_load(camera_disk)&&camera_disk->turns[camera_disk->count-1].id==before_bot);
  observation_fail=false;send_mode("normal-after-bot",AGENT_MODE_CHAT);
  assert(!glass_chat_store_load(camera_disk));
  for(unsigned i=0;i<camera_disk->count;i++)assert(camera_disk->turns[i].id!=bot_id&&!camera_disk->turns[i].session_history);
  before_bot=camera_disk->turns[camera_disk->count-1].id;
  long_answer=true;assert(!glass_chat_send_bot("long-bot",AGENT_MODE_ASSISTANT));idle();long_answer=false;
  assert(state.turns[state.count-1].state==CHAT_DONE&&state.turns[state.count-1].clipped);
  assert(!strncmp(state.turns[state.count-1].archive,"sd-reply-",9)&&state.turns[state.count-1].archive_bytes>CHAT_REPLY_BYTES);
  assert(!glass_chat_store_load(camera_disk)&&camera_disk->turns[camera_disk->count-1].id==before_bot);
  free(camera_disk);
  persisted_last_id=before_bot;
  assert(!glass_chat_send_bot("agent-look",AGENT_MODE_ASSISTANT));idle();
  assert(state.turns[state.count-1].state==CHAT_DONE&&state.turns[state.count-1].camera_tool_used);
  assert(strstr(state.turns[state.count-1].reply,"continued answer")&&state.turns[state.count-1].camera_recording[0]);
  unsigned uploads_before=camera_uploads;
  recording_fail=true;assert(!glass_chat_send_camera("camera",AGENT_MODE_ASSISTANT));idle();recording_fail=false;
  assert(state.turns[state.count-1].error==CHAT_ERROR_CAMERA_STORAGE&&camera_uploads==uploads_before);
  /* Both slots invalid: the load reports failure instead of guessing. */
  for(unsigned i=0;i<2;i++){
    f=fopen(i?CHAT_DATA_ROOT "/chat/history.tmp":CHAT_DATA_ROOT "/chat/history.json","w");assert(f);
    fputs("{\"version\":1,\"sequence\":9,\"turns\":[{\"id\":1,\"state\":1,\"error\":0,\"prompt\":\"p\",\"reply\":\"r\",\"cards\":[{\"kind\":\"light\",\"name\":\"x\",\"state\":\"y\",\"level\":-2,\"on\":true}]}]}",f);fclose(f);
  }
  struct chat_snapshot *broken=calloc(1,sizeof(*broken));assert(broken);
  assert(glass_chat_store_load(broken)!=0&&broken->count==0);free(broken);
  puts("AGENT_MODES_OK: migration, durable mode tags, context isolation, same-mode UI history, scoped clear, retry identity, immutable queued/request mode, cancellation, card persistence");
  return 0;
}
