/* SPDX-License-Identifier: Apache-2.0 */
/* Real service, JSON, durable storage and scheduler; only devices are fixtures. */
#include <assert.h>
#include "../overlay/apps/system/desktop/glass_board_assistant.c"
static const struct glass_board_home *binding;
static unsigned effects;
static unsigned write_fail_after;
static bool home_allowed=true;
int test_storage_write_sync_impl(const char *,const char *,const char *,const char *,size_t);
int qpk_storage_write_sync(const char *r,const char *p,const char *k,const char *v,size_t n)
{
  if(write_fail_after&&!--write_fail_after)return -EIO;
  return test_storage_write_sync_impl(r,p,k,v,n);
}
void glass_agent_set_board_assistant(const struct glass_board_home *p){binding=p;}
char *glass_assistant_platform(const char *tool,const cJSON *args,atomic_bool *cancel)
{(void)tool;(void)args;(void)cancel;return strdup("{\"ok\":true}");}
bool glass_assistant_action_valid(const char *action,const cJSON *args)
{
  if(!strcmp(action,"music_stop"))return only(args,"");
  if(!strcmp(action,"notify"))return only(args,",text,")&&text(field(args,"text"),512,true,false);
  if(!strcmp(action,"home_control"))return only(args,",entity,,on,")&&text(field(args,"entity"),95,false,false)&&cJSON_IsBool(field(args,"on"));
  return false;
}
char *glass_assistant_action(const char *action,const cJSON *args,atomic_bool *cancel)
{
  (void)cancel;
  if(!strcmp(action,"home_control")&&!home_allowed)return strdup("{\"ok\":false,\"error\":\"home_control_disabled\"}");
  effects++;if(!strcmp(action,"notify"))glass_assistant_notice(str(args,"text"));
  return strdup("{\"ok\":true,\"verified\":true}");
}
static cJSON *invoke(const char *tool,const char *args,bool failed)
{
  cJSON *a=cJSON_Parse(args);assert(a);atomic_bool cancel=false;
  char *response=binding->call(tool,a,&cancel);cJSON_Delete(a);assert(response);
  cJSON *r=cJSON_Parse(response);free(response);assert(r);
  if((field(r,"error")!=NULL)!=failed){char *s=cJSON_PrintUnformatted(r);fprintf(stderr,"unexpected %s %s => %s\n",tool,args,s);free(s);abort();}
  return r;
}
static cJSON *record(const char *args,bool failed){return invoke("assistant_records",args,failed);}
static cJSON *schedule(const char *args,bool failed){return invoke("assistant_schedule",args,failed);}
static unsigned add_schedule(const char *request,int delay,const char *action,const char *arguments)
{
  char json[1024];snprintf(json,sizeof(json),"{\"operation\":\"add\",\"request_id\":\"%s\",\"title\":\"提醒\",\"delay_seconds\":%d,\"action\":\"%s\",\"arguments\":%s}",request,delay,action,arguments);
  cJSON *r=schedule(json,false);unsigned id=(unsigned)num(field(r,"item"),"id");cJSON_Delete(r);return id;
}
static void item_state(unsigned id,const char *state)
{
  pthread_mutex_lock(&data_lock);cJSON *doc=load("schedules");assert(doc);cJSON *r=find(doc,id);assert(r);assert(!strcmp(str(r,"state"),state));cJSON_Delete(doc);pthread_mutex_unlock(&data_lock);
}
int main(int argc,char **argv)
{
  glass_board_assistant_register();assert(binding);
  if(argc==2&&!strcmp(argv[1],"--restart")){
    cJSON *r=record("{\"operation\":\"list\",\"kind\":\"note\",\"query\":\"updated\"}",false);
    assert(num(r,"total")==1);cJSON_Delete(r);assert(!glass_assistant_tick(time(NULL)));
    assert(effects==0);puts("ASSISTANT_RESTART_OK: retained notes; interrupted tasks never replay");return 0;
  }
  char *catalog=malloc(strlen(binding->catalog)+3);sprintf(catalog,"[%s]",binding->catalog);cJSON *schemas=cJSON_Parse(catalog);
  assert(cJSON_GetArraySize(schemas)==10);free(catalog);cJSON_Delete(schemas);assert(!glass_assistant_start());
  cJSON *r=record("{\"operation\":\"add\",\"kind\":\"note\",\"request_id\":\"n1\",\"title\":\"购物\",\"text\":\"牛奶\\n面包\"}",false);
  unsigned id=(unsigned)num(field(r,"item"),"id");assert(id==1);cJSON_Delete(r);
  r=record("{\"operation\":\"add\",\"kind\":\"note\",\"request_id\":\"n1\",\"title\":\"购物\",\"text\":\"牛奶\\n面包\"}",false);
  assert(cJSON_IsTrue(field(r,"already_saved")));cJSON_Delete(r);
  cJSON_Delete(record("{\"operation\":\"add\",\"kind\":\"note\",\"request_id\":\"n1\",\"title\":\"different\"}",true));
  cJSON_Delete(record("{\"operation\":\"update\",\"kind\":\"note\",\"id\":1,\"title\":\"updated\"}",false));
  cJSON_Delete(record("{\"operation\":\"list\",\"kind\":\"note\",\"offset\":-1}",true));
  cJSON_Delete(record("{\"operation\":\"add\",\"kind\":\"note\",\"request_id\":\"bad\",\"title\":\"x\",\"done\":true}",true));
  cJSON_Delete(record("{\"operation\":\"add\",\"kind\":\"todo\",\"request_id\":\"t1\",\"title\":\"买牛奶\"}",false));
  r=record("{\"operation\":\"update\",\"kind\":\"todo\",\"id\":1,\"done\":true}",false);assert(cJSON_IsTrue(field(field(r,"item"),"done")));cJSON_Delete(r);
  cJSON_Delete(record("{\"operation\":\"add\",\"kind\":\"event\",\"request_id\":\"e1\",\"title\":\"meeting\"}",true));
  cJSON_Delete(record("{\"operation\":\"add\",\"kind\":\"event\",\"request_id\":\"e1\",\"title\":\"meeting\",\"due_at\":1800000000,\"end_at\":1800003600}",false));
  r=record("{\"operation\":\"list\",\"kind\":\"event\",\"from_at\":1800001000,\"until_at\":1800002000}",false);assert(num(r,"total")==1);cJSON_Delete(r);
  cJSON_Delete(record("{\"operation\":\"update\",\"kind\":\"event\",\"id\":1,\"end_at\":1}",true));
  cJSON_Delete(record("{\"operation\":\"delete\",\"kind\":\"event\",\"id\":1}",false));
  for(unsigned i=1;i<16;i++){char json[256];snprintf(json,sizeof(json),"{\"operation\":\"add\",\"kind\":\"todo\",\"request_id\":\"t%u\",\"title\":\"item%u\"}",i+1,i);cJSON_Delete(record(json,false));}
  r=record("{\"operation\":\"list\",\"kind\":\"todo\"}",false);assert(num(r,"total")==16&&num(r,"next_offset")==8&&cJSON_GetArraySize(field(r,"items"))==8);cJSON_Delete(r);
  cJSON_Delete(record("{\"operation\":\"add\",\"kind\":\"todo\",\"request_id\":\"overflow\",\"title\":\"x\"}",true));
  write_fail_after=1;cJSON_Delete(record("{\"operation\":\"update\",\"kind\":\"note\",\"id\":1,\"title\":\"must not commit\"}",true));
  r=record("{\"operation\":\"list\",\"kind\":\"note\",\"query\":\"updated\"}",false);assert(num(r,"total")==1);cJSON_Delete(r);
  puts("ASSISTANT_RECORDS_OK: durable notes/todos/events, idempotent create, conflicts, UTF-8, updates/deletion, event overlap, pagination, capacity, failed-write preservation");

  cJSON_Delete(invoke("assistant_alias","{\"operation\":\"save\",\"alias\":\"大灯\",\"kind\":\"home\",\"target\":\"light.living\"}",false));
  cJSON_Delete(invoke("assistant_alias","{\"operation\":\"save\",\"alias\":\"小灯\",\"kind\":\"home\",\"target\":\"light.desk\"}",false));
  r=invoke("assistant_alias","{\"operation\":\"lookup\",\"alias\":\"灯\"}",false);assert(!cJSON_IsTrue(field(r,"exact"))&&cJSON_GetArraySize(field(r,"items"))==0);cJSON_Delete(r);
  r=invoke("assistant_alias","{\"operation\":\"lookup\",\"alias\":\"关一下大灯\"}",false);assert(cJSON_GetArraySize(field(r,"items"))==0);cJSON_Delete(r);
  r=invoke("assistant_alias","{\"operation\":\"lookup\",\"alias\":\"大灯\"}",false);assert(cJSON_IsTrue(field(r,"exact"))&&!cJSON_IsTrue(field(r,"ambiguous")));cJSON_Delete(r);
  cJSON_Delete(invoke("assistant_alias","{\"operation\":\"save\",\"alias\":\"bad\",\"kind\":\"home\",\"target\":\"light.desk,light.other\"}",true));
  cJSON_Delete(invoke("assistant_alias","{\"operation\":\"delete\",\"alias\":\"小灯\"}",false));assert(!effects);
  puts("ASSISTANT_ALIASES_OK: exact names only, substring and sentence rejection without effects, deletion and target validation");

  time_t now=time(NULL);unsigned first=add_schedule("s1",1,"notify","{\"text\":\"喝水\"}");
  assert(add_schedule("s1",1,"notify","{\"text\":\"喝水\"}")==first);
  assert(!glass_assistant_tick(now+2));assert(effects==1);item_state(first,"done");
  char notice[256];assert(glass_assistant_take_notice(notice,sizeof(notice))&&!strcmp(notice,"喝水"));assert(!glass_assistant_take_notice(notice,sizeof(notice)));
  assert(!glass_assistant_tick(now+3)&&effects==1);
  unsigned cancelled=add_schedule("cancel",1,"music_stop","{}");char json[128];
  snprintf(json,sizeof(json),"{\"operation\":\"cancel\",\"id\":%u}",cancelled);cJSON_Delete(schedule(json,false));
  assert(!glass_assistant_tick(now+4)&&effects==1);item_state(cancelled,"cancelled");
  unsigned missed=add_schedule("missed",1,"music_stop","{}");assert(!glass_assistant_tick(now+300)&&effects==1);item_state(missed,"missed");
  unsigned guarded=add_schedule("guarded",1,"music_stop","{}");
  write_fail_after=1;assert(glass_assistant_tick(now+5)==-EIO&&effects==1);item_state(guarded,"pending");
  write_fail_after=2;assert(glass_assistant_tick(now+5)==-EIO&&effects==2);item_state(guarded,"executing");
  assert(!glass_assistant_tick(now+6)&&effects==2);item_state(guarded,"interrupted");
  unsigned home=add_schedule("home",1,"home_control","{\"entity\":\"light.desk\",\"on\":false}");
  home_allowed=false;assert(!glass_assistant_tick(now+7)&&effects==2);item_state(home,"failed");
  cJSON_Delete(schedule("{\"operation\":\"add\",\"request_id\":\"badtime\",\"title\":\"x\",\"daily_at\":\"25:00\",\"action\":\"music_stop\",\"arguments\":{}}",true));
  cJSON_Delete(schedule("{\"operation\":\"add\",\"request_id\":\"both\",\"title\":\"x\",\"daily_at\":\"22:00\",\"delay_seconds\":2,\"action\":\"music_stop\",\"arguments\":{}}",true));
  r=schedule("{\"operation\":\"add\",\"request_id\":\"daily\",\"title\":\"sleep\",\"daily_at\":\"22:00\",\"action\":\"music_stop\",\"arguments\":{}}",false);
  unsigned daily=(unsigned)num(field(r,"item"),"id");time_t due=(time_t)num(field(r,"item"),"due_at");assert(due>now&&due<=now+86400);cJSON_Delete(r);
  r=schedule("{\"operation\":\"add\",\"request_id\":\"daily-utc\",\"title\":\"utc\",\"daily_at\":\"22:00\",\"timezone_offset_minutes\":0,\"action\":\"music_stop\",\"arguments\":{}}",false);
  unsigned utc_id=(unsigned)num(field(r,"item"),"id");cJSON_Delete(r);
  cJSON_Delete(schedule("{\"operation\":\"add\",\"request_id\":\"daily-utc\",\"title\":\"utc\",\"daily_at\":\"22:00\",\"action\":\"music_stop\",\"arguments\":{}}",true));
  snprintf(json,sizeof(json),"{\"operation\":\"cancel\",\"id\":%u}",utc_id);cJSON_Delete(schedule(json,false));
  assert(!glass_assistant_tick(due+1)&&effects==3);item_state(daily,"done");
  pthread_mutex_lock(&data_lock);cJSON *doc=load("schedules");assert(num(find(doc,daily),"due_at")==due+86400);
  cJSON *pending=find(doc,daily);set_string(pending,"state","executing");assert(!save("schedules",doc));cJSON_Delete(doc);pthread_mutex_unlock(&data_lock);
  puts("ASSISTANT_SCHEDULES_OK: no-chat execution, one-shot/daily, no duplicate effects, cancellation, missed deadlines, before/after-effect storage failures, interrupted recovery, execution-time permission");
  atomic_bool cancel=true;cJSON *empty=cJSON_CreateObject();char *out=binding->call("assistant_records",empty,&cancel);assert(out&&strstr(out,"cancelled"));free(out);cJSON_Delete(empty);
  assert(glass_assistant_tick(1)==-EAGAIN);
  char sdparent[128],away[160];snprintf(sdparent,sizeof(sdparent),"%s",ASSISTANT_SD_ROOT);*strrchr(sdparent,'/')=0;
  assert(!mkdir(sdparent,0700));
  r=record("{\"operation\":\"list\",\"kind\":\"note\",\"query\":\"updated\"}",false);assert(num(r,"total")==1);cJSON_Delete(r);
  cJSON_Delete(record("{\"operation\":\"add\",\"kind\":\"note\",\"request_id\":\"sd-only\",\"title\":\"SD only\"}",false));
  assert(!strcmp(data_root,ASSISTANT_SD_ROOT));
  r=schedule("{\"operation\":\"list\"}",false);assert(!cJSON_GetArraySize(field(r,"items")));cJSON_Delete(r);
  unsigned prior_effects=effects;unsigned sdjob=add_schedule("sd-job",1,"music_stop","{}");
  assert(!glass_assistant_tick(time(NULL)+2)&&effects==prior_effects+1);item_state(sdjob,"done");
  snprintf(away,sizeof(away),"%s-away",sdparent);assert(!rename(sdparent,away));
  r=record("{\"operation\":\"list\",\"kind\":\"note\",\"query\":\"SD only\"}",false);assert(num(r,"total")==0);cJSON_Delete(r);
  assert(!rename(away,sdparent));
  snprintf(away,sizeof(away),"%s-away",ASSISTANT_SD_ROOT);assert(!rename(ASSISTANT_SD_ROOT,away));
  assert(!symlink(ASSISTANT_DATA_ROOT,ASSISTANT_SD_ROOT));
  cJSON_Delete(record("{\"operation\":\"add\",\"kind\":\"note\",\"request_id\":\"bad-sd\",\"title\":\"must fail\"}",true));
  assert(!unlink(ASSISTANT_SD_ROOT)&&!rename(away,ASSISTANT_SD_ROOT));
  puts("ASSISTANT_SD_OK: SD-first updates preserve legacy records, no Flash spill on SD fault; schedules isolated per medium and not copied/replayed");
  puts("ASSISTANT_SERVICE_OK");return 0;
}
