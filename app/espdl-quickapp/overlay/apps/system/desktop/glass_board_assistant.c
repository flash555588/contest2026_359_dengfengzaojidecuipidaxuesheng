/* SPDX-License-Identifier: Apache-2.0 */
/* Local records and deterministic schedules. No model or LVGL on this worker. */
#include "glass_assistant.h"
#include "glass_agent.h"
#include "glass_chat.h"
#include "glass_assistant_tools.h"
#include "qpk_storage.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "qpk_photo_storage.h"

#ifndef ASSISTANT_DATA_ROOT
#define ASSISTANT_DATA_ROOT "/data/assistant"
#endif
#ifndef ASSISTANT_SD_ROOT
#define ASSISTANT_SD_ROOT "/sdcard/espclaw-assistant"
#endif
#define ASSISTANT_PACKAGE "com.openvela.assistant"
#define ITEMS_MAX 16
#define ASSISTANT_TIME_MIN 1735689600LL
#define ASSISTANT_TIME_MAX 4102444800LL
static pthread_mutex_t data_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t tick_lock=PTHREAD_MUTEX_INITIALIZER;
static bool scheduler_started;
static int scheduler_error;
static double schedule_next_scan;
/* Accessed only under data_lock; one load/save transaction pins its medium. */
static const char *data_root=ASSISTANT_DATA_ROOT;
static int select_data_root(void)
{
  int ret=qpk_volume_mounted(QPK_VOLUME_SD);
  const char *next=ASSISTANT_DATA_ROOT;
  if(!ret){
    char parent[128];struct stat st;
    ret=qpk_photo_parent(ASSISTANT_SD_ROOT,parent,sizeof(parent));if(ret)return ret;
    if(!lstat(ASSISTANT_SD_ROOT,&st)){
      if(!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return -EPERM;
    }else if(errno!=ENOENT)return -errno;
    next=ASSISTANT_SD_ROOT;
  }else if(ret!=-ENODEV&&ret!=-ENOENT&&ret!=-ENXIO)return ret;
  if(strcmp(data_root,next))schedule_next_scan=0;
  data_root=next;return 0;
}
static pthread_mutex_t notice_lock=PTHREAD_MUTEX_INITIALIZER;
static char (*notices)[256];
static unsigned notice_first,notice_count;

static const cJSON *field(const cJSON *o,const char *key)
{return cJSON_GetObjectItemCaseSensitive(o,key);}
static const char *str(const cJSON *o,const char *key)
{const cJSON *v=field(o,key);return cJSON_IsString(v)?v->valuestring:"";}
static double num(const cJSON *o,const char *key)
{const cJSON *v=field(o,key);return cJSON_IsNumber(v)?v->valuedouble:0;}
static bool integer(const cJSON *v,double low,double high)
{return cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=low&&v->valuedouble<=high&&floor(v->valuedouble)==v->valuedouble;}
static bool text(const cJSON *v,size_t cap,bool multiline,bool empty)
{return cJSON_IsString(v)&&(empty||v->valuestring[0])&&glass_chat_utf8(v->valuestring,cap,multiline);}
static bool only(const cJSON *o,const char *allowed)
{
  if(!cJSON_IsObject(o))return false;
  for(const cJSON *v=o->child;v;v=v->next){
    if(!v->string||strlen(v->string)>40||field(o,v->string)!=v)return false;
    char key[48];snprintf(key,sizeof(key),",%s,",v->string);if(!strstr(allowed,key))return false;
  }
  return true;
}
static char *finish(cJSON *o)
{char *s=o?cJSON_PrintUnformatted(o):NULL;cJSON_Delete(o);return s;}
static cJSON *ok(void)
{cJSON *o=cJSON_CreateObject();if(o)cJSON_AddBoolToObject(o,"ok",true);return o;}
static char *failure(const char *code)
{cJSON *o=cJSON_CreateObject();if(o){cJSON_AddBoolToObject(o,"ok",false);cJSON_AddStringToObject(o,"error",code);}return finish(o);}
static void set_string(cJSON *o,const char *key,const char *value)
{cJSON_DeleteItemFromObjectCaseSensitive(o,key);cJSON_AddStringToObject(o,key,value);}
static void set_number(cJSON *o,const char *key,double value)
{cJSON_DeleteItemFromObjectCaseSensitive(o,key);cJSON_AddNumberToObject(o,key,value);}
static void set_bool(cJSON *o,const char *key,bool value)
{cJSON_DeleteItemFromObjectCaseSensitive(o,key);cJSON_AddBoolToObject(o,key,value);}
static bool identifier(const cJSON *v)
{
  if(!text(v,64,false,false))return false;
  for(const unsigned char *p=(const unsigned char *)v->valuestring;*p;p++)
    if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'))return false;
  return true;
}
static bool contains(const char *value,const char *query)
{
  if(!query[0])return true;
  for(;*value;value++){
    const unsigned char *a=(const unsigned char *)value,*b=(const unsigned char *)query;
    while(*a&&*b&&tolower(*a)==tolower(*b)){a++;b++;}
    if(!*b)return true;
  }
  return false;
}
static bool record_valid(const cJSON *r,const char *kind)
{
  if(!only(r,",id,,request_id,,title,,text,,due_at,,end_at,,done,"))return false;
  if(!integer(field(r,"id"),1,2147483646)||!identifier(field(r,"request_id"))||!text(field(r,"title"),96,false,false))return false;
  if(!text(field(r,"text"),512,true,true))return false;
  if(field(r,"due_at")&&!integer(field(r,"due_at"),0,ASSISTANT_TIME_MAX))return false;
  if(field(r,"end_at")&&(!integer(field(r,"end_at"),0,ASSISTANT_TIME_MAX)||!field(r,"due_at")||num(r,"end_at")<num(r,"due_at")))return false;
  if(!strcmp(kind,"event")&&!integer(field(r,"due_at"),ASSISTANT_TIME_MIN,ASSISTANT_TIME_MAX))return false;
  if(field(r,"done")&&(strcmp(kind,"todo")||!cJSON_IsBool(field(r,"done"))))return false;
  return true;
}
static bool alias_valid(const cJSON *r)
{
  if(!only(r,",alias,,kind,,target,")||!text(field(r,"alias"),64,false,false)||!text(field(r,"target"),127,false,false))return false;
  const char *kind=str(r,"kind"),*target=str(r,"target");
  if(strcmp(kind,"home")&&strcmp(kind,"app")&&strcmp(kind,"music"))return false;
  if(strstr(target,"://")||target[0]=='/'||strchr(target,'\\'))return false;
  if(!strcmp(kind,"home")){
    unsigned dots=0;for(const char *p=target;*p;p++){
      if(*p=='.'){if(p==target||!p[1])return false;dots++;}
      else if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'))return false;
    }if(dots!=1)return false;
  }
  return true;
}
static bool schedule_valid(const cJSON *r)
{
  if(!only(r,",id,,request_id,,title,,action,,arguments,,due_at,,initial_due_at,,delay_seconds,,daily_at,,timezone_offset_minutes,,active,,state,,last_run,,result,"))return false;
  if(!integer(field(r,"id"),1,2147483646)||!identifier(field(r,"request_id"))||!text(field(r,"title"),96,false,false)||
     !integer(field(r,"due_at"),ASSISTANT_TIME_MIN,ASSISTANT_TIME_MAX)||!integer(field(r,"initial_due_at"),ASSISTANT_TIME_MIN,ASSISTANT_TIME_MAX)||
     !cJSON_IsBool(field(r,"active"))||!glass_assistant_action_valid(str(r,"action"),field(r,"arguments")))return false;
  const char *state=str(r,"state");
  if(strcmp(state,"pending")&&strcmp(state,"executing")&&strcmp(state,"done")&&strcmp(state,"failed")&&strcmp(state,"missed")&&strcmp(state,"interrupted")&&strcmp(state,"cancelled"))return false;
  if(field(r,"delay_seconds")&&!integer(field(r,"delay_seconds"),1,31536000))return false;
  if(field(r,"daily_at")){
    const char *at=str(r,"daily_at");
    if(strlen(at)!=5||at[2]!=':'||at[0]<'0'||at[0]>'2'||at[1]<'0'||at[1]>'9'||at[3]<'0'||at[3]>'5'||at[4]<'0'||at[4]>'9'||atoi(at)>23)return false;
    if(!integer(field(r,"timezone_offset_minutes"),-720,840))return false;
  }
  return !field(r,"result")||cJSON_IsObject(field(r,"result"));
}
static bool document_valid(const cJSON *doc,const char *kind)
{
  bool valid=only(doc,",version,,sequence,,next_id,,items,")&&num(doc,"version")==1&&integer(field(doc,"sequence"),0,9007199254740990.0)&&integer(field(doc,"next_id"),1,2147483647)&&
    cJSON_IsArray(field(doc,"items"))&&cJSON_GetArraySize(field(doc,"items"))<=ITEMS_MAX;
  for(const cJSON *r=valid?field(doc,"items")->child:NULL;r&&valid;r=r->next){
    valid=!strcmp(kind,"aliases")?alias_valid(r):!strcmp(kind,"schedules")?schedule_valid(r):record_valid(r,kind);
    if(strcmp(kind,"aliases"))valid&=num(r,"id")<num(doc,"next_id");
    for(const cJSON *prev=field(doc,"items")->child;prev!=r;prev=prev->next)
      if(!strcmp(kind,"aliases")?!strcmp(str(prev,"alias"),str(r,"alias")):num(prev,"id")==num(r,"id")||!strcmp(str(prev,"request_id"),str(r,"request_id")))valid=false;
  }
  return valid;
}
/* Two independent slots survive SmartFS's unlink-before-rename behavior. */
static cJSON *load_at(const char *kind,const char *root,bool legacy)
{
  char *buffer=malloc(QPK_STORAGE_VALUE_MAX+1);if(!buffer)return NULL;
  cJSON *latest=NULL;unsigned missing=0;bool corrupt=false;
  for(unsigned slot=0;slot<2;slot++){
    char key[24];snprintf(key,sizeof(key),"%s%u",kind,slot);size_t size=0;
    int ret=qpk_storage_read(root,ASSISTANT_PACKAGE,key,buffer,QPK_STORAGE_VALUE_MAX+1,&size);
    if(ret==-ENOENT){missing++;continue;}
    cJSON *doc=NULL;
    if(!ret&&size<=QPK_STORAGE_VALUE_MAX&&!memchr(buffer,0,size)){buffer[size]=0;doc=glass_agent_json(buffer);}
    if(!document_valid(doc,kind)){corrupt=true;cJSON_Delete(doc);continue;}
    if(!latest||num(doc,"sequence")>num(latest,"sequence")){cJSON_Delete(latest);latest=doc;}
    else{if(num(doc,"sequence")==num(latest,"sequence")&&!cJSON_Compare(doc,latest,true))corrupt=true;cJSON_Delete(doc);}
  }
  free(buffer);
  /* Notes/aliases remain readable without deleting old files. Schedules are
   * separate per medium: copying a pending task could execute its effect twice. */
  if(!latest&&missing==2&&legacy&&strcmp(kind,"schedules"))return load_at(kind,ASSISTANT_DATA_ROOT,false);
  /* Never fall back to a possibly pending pre-effect snapshot of schedules. */
  if(corrupt&&!strcmp(kind,"schedules")){cJSON_Delete(latest);return NULL;}
  if(!latest&&missing==2){latest=cJSON_CreateObject();if(latest){cJSON_AddNumberToObject(latest,"version",1);
    cJSON_AddNumberToObject(latest,"sequence",0);cJSON_AddNumberToObject(latest,"next_id",1);cJSON_AddArrayToObject(latest,"items");}}
  return latest;
}
static cJSON *load(const char *kind)
{
  if(select_data_root())return NULL;
  return load_at(kind,data_root,!strcmp(data_root,ASSISTANT_SD_ROOT));
}
static int save(const char *kind,cJSON *doc)
{
  if(!strcmp(data_root,ASSISTANT_SD_ROOT)){
    int ret=qpk_volume_mounted(QPK_VOLUME_SD);if(ret)return ret;
    char directory[128];ret=qpk_photo_directory(directory,sizeof(directory),QPK_STORAGE_VALUE_MAX+1024,ASSISTANT_SD_ROOT);if(ret)return ret;
  }
  if(num(doc,"sequence")>=9007199254740990.0)return -EOVERFLOW;
  set_number(doc,"sequence",num(doc,"sequence")+1);
  char *buffer=cJSON_PrintUnformatted(doc);if(!buffer)return -ENOMEM;
  char key[24];snprintf(key,sizeof(key),"%s%u",kind,(unsigned)fmod(num(doc,"sequence"),2));
  size_t size=strlen(buffer);int ret=size>QPK_STORAGE_VALUE_MAX?-EFBIG:
    qpk_storage_write_sync(data_root,ASSISTANT_PACKAGE,key,buffer,size);
  if(!strcmp(kind,"schedules"))schedule_next_scan=0;
  free(buffer);return ret;
}
static cJSON *find(cJSON *doc,unsigned id)
{for(cJSON *r=field(doc,"items")->child;r;r=r->next)if(num(r,"id")==id)return r;return NULL;}
static char *saved_result(const char *kind,cJSON *doc,cJSON *record)
{
  int ret=save(kind,doc);cJSON *out=ret?NULL:ok();
  if(out&&record)cJSON_AddItemToObject(out,"item",cJSON_Duplicate(record,true));
  cJSON_Delete(doc);return ret?failure(ret==-EFBIG?"local_storage_full":"local_storage_write_failed"):finish(out);
}
static char *records(const cJSON *args)
{
  const char *kind=str(args,"kind"),*op=str(args,"operation");
  if(strcmp(kind,"note")&&strcmp(kind,"todo")&&strcmp(kind,"event"))return failure("invalid_arguments");
  const char *keys=!strcmp(op,"list")?",operation,,kind,,query,,offset,,from_at,,until_at,":
    !strcmp(op,"add")?",operation,,kind,,request_id,,title,,text,,due_at,,end_at,,done,":
    !strcmp(op,"update")?",operation,,kind,,id,,title,,text,,due_at,,end_at,,done,":
    !strcmp(op,"delete")?",operation,,kind,,id,":NULL;
  if(!keys||!only(args,keys))return failure("invalid_arguments");
  if(field(args,"query")&&!text(field(args,"query"),96,false,true))return failure("invalid_arguments");
  if(field(args,"offset")&&!integer(field(args,"offset"),0,100000))return failure("invalid_arguments");
  if((field(args,"from_at")&&!integer(field(args,"from_at"),0,ASSISTANT_TIME_MAX))||
     (field(args,"until_at")&&!integer(field(args,"until_at"),0,ASSISTANT_TIME_MAX))||
     (field(args,"from_at")&&field(args,"until_at")&&num(args,"from_at")>num(args,"until_at")))return failure("invalid_arguments");
  cJSON *doc=load(kind);if(!doc)return failure("local_storage_unavailable_or_corrupt");
  if(!strcmp(op,"list")){
    cJSON *out=ok(),*items=cJSON_AddArrayToObject(out,"items");unsigned total=0,offset=(unsigned)num(args,"offset");
    for(const cJSON *r=field(doc,"items")->child;r;r=r->next){
      if(!contains(str(r,"title"),str(args,"query"))&&!contains(str(r,"text"),str(args,"query")))continue;
      if(field(args,"from_at")&&(!field(r,"due_at")||(field(r,"end_at")?num(r,"end_at"):num(r,"due_at"))<num(args,"from_at")))continue;
      if(field(args,"until_at")&&(!field(r,"due_at")||num(r,"due_at")>num(args,"until_at")))continue;
      if(total++>=offset&&cJSON_GetArraySize(items)<8)cJSON_AddItemToArray(items,cJSON_Duplicate(r,true));
    }
    cJSON_AddNumberToObject(out,"total",total);if(total>offset+8)cJSON_AddNumberToObject(out,"next_offset",offset+8);
    cJSON_AddStringToObject(out,"storage","device_local");cJSON_Delete(doc);return finish(out);
  }
  cJSON *r=NULL;
  if(!strcmp(op,"add")){
    if(!identifier(field(args,"request_id"))){cJSON_Delete(doc);return failure("invalid_arguments");}
    for(r=field(doc,"items")->child;r;r=r->next)if(!strcmp(str(r,"request_id"),str(args,"request_id")))break;
    if(r){
      bool same=!strcmp(str(r,"title"),str(args,"title"))&&!strcmp(str(r,"text"),str(args,"text"));
      const char *optional[]={"due_at","end_at","done"};
      for(unsigned i=0;i<3;i++)if(field(args,optional[i]))same&=cJSON_Compare(field(r,optional[i]),field(args,optional[i]),true);
      cJSON *out=same?ok():NULL;if(out){cJSON_AddBoolToObject(out,"already_saved",true);cJSON_AddItemToObject(out,"item",cJSON_Duplicate(r,true));}
      cJSON_Delete(doc);return same?finish(out):failure("request_id_conflict");
    }
    if(cJSON_GetArraySize(field(doc,"items"))>=ITEMS_MAX||num(doc,"next_id")>=2147483647){cJSON_Delete(doc);return failure("local_record_limit");}
    r=cJSON_CreateObject();cJSON_AddNumberToObject(r,"id",num(doc,"next_id"));set_number(doc,"next_id",num(doc,"next_id")+1);
    cJSON_AddStringToObject(r,"request_id",str(args,"request_id"));cJSON_AddStringToObject(r,"text","");
    if(!strcmp(kind,"todo"))cJSON_AddBoolToObject(r,"done",false);
    cJSON_AddItemToArray((cJSON *)field(doc,"items"),r);
  }else{
    if(!integer(field(args,"id"),1,2147483646)){cJSON_Delete(doc);return failure("invalid_arguments");}
    r=find(doc,(unsigned)num(args,"id"));if(!r){cJSON_Delete(doc);return failure("record_not_found");}
    if(!strcmp(op,"delete")){cJSON_DetachItemViaPointer((cJSON *)field(doc,"items"),r);cJSON_Delete(r);return saved_result(kind,doc,NULL);}
    if(cJSON_GetArraySize(args)<4){cJSON_Delete(doc);return failure("missing_update_fields");}
  }
  const char *copy[]={"title","text","due_at","end_at","done"};
  for(unsigned i=0;i<5;i++)if(field(args,copy[i])){
    cJSON_DeleteItemFromObjectCaseSensitive(r,copy[i]);cJSON_AddItemToObject(r,copy[i],cJSON_Duplicate(field(args,copy[i]),true));
  }
  if(!record_valid(r,kind)){cJSON_Delete(doc);return failure("invalid_arguments");}
  return saved_result(kind,doc,r);
}

static char *aliases(const cJSON *args)
{
  const char *op=str(args,"operation");
  const char *keys=!strcmp(op,"list")?",operation,":!strcmp(op,"lookup")?",operation,,alias,,kind,":
    !strcmp(op,"save")?",operation,,alias,,kind,,target,":!strcmp(op,"delete")?",operation,,alias,":NULL;
  if(!keys||!only(args,keys)||
     (strcmp(op,"list")&&!text(field(args,"alias"),64,false,false)))return failure("invalid_arguments");
  if(field(args,"kind")&&strcmp(str(args,"kind"),"home")&&strcmp(str(args,"kind"),"app")&&strcmp(str(args,"kind"),"music"))return failure("invalid_arguments");
  cJSON *doc=load("aliases");if(!doc)return failure("local_storage_unavailable_or_corrupt");
  if(!strcmp(op,"list")||!strcmp(op,"lookup")){
    cJSON *out=ok(),*items=cJSON_AddArrayToObject(out,"items");bool exact=false;
    for(cJSON *r=field(doc,"items")->child;r;r=r->next){
      if(field(args,"kind")&&strcmp(str(args,"kind"),str(r,"kind")))continue;
      if(!strcmp(op,"lookup")&&strcmp(str(r,"alias"),str(args,"alias")))continue;
      if(!strcmp(op,"lookup"))exact=true;
      cJSON_AddItemToArray(items,cJSON_Duplicate(r,true));
    }
    cJSON_AddBoolToObject(out,"ambiguous",cJSON_GetArraySize(items)>1);cJSON_AddBoolToObject(out,"exact",exact);
    cJSON_Delete(doc);return finish(out);
  }
  cJSON *r;for(r=field(doc,"items")->child;r;r=r->next)if(!strcmp(str(r,"alias"),str(args,"alias")))break;
  if(!strcmp(op,"delete")){
    if(!r){cJSON_Delete(doc);return failure("alias_not_found");}
    cJSON_DetachItemViaPointer((cJSON *)field(doc,"items"),r);cJSON_Delete(r);return saved_result("aliases",doc,NULL);
  }
  cJSON *copy=cJSON_Duplicate(args,true);cJSON_DeleteItemFromObjectCaseSensitive(copy,"operation");
  if(!alias_valid(copy)){cJSON_Delete(copy);cJSON_Delete(doc);return failure("invalid_arguments");}
  if(r)cJSON_ReplaceItemViaPointer((cJSON *)field(doc,"items"),r,copy);
  else if(cJSON_GetArraySize(field(doc,"items"))>=ITEMS_MAX){cJSON_Delete(copy);cJSON_Delete(doc);return failure("local_record_limit");}
  else cJSON_AddItemToArray((cJSON *)field(doc,"items"),copy);
  return saved_result("aliases",doc,copy);
}

static char *schedules(const cJSON *args)
{
  const char *op=str(args,"operation");
  const char *keys=!strcmp(op,"list")?",operation,,offset,":!strcmp(op,"add")?
    ",operation,,request_id,,title,,action,,arguments,,delay_seconds,,due_at,,daily_at,,timezone_offset_minutes,":
    !strcmp(op,"cancel")||!strcmp(op,"delete")?",operation,,id,":NULL;
  if(!keys||!only(args,keys)||(field(args,"offset")&&!integer(field(args,"offset"),0,100000)))return failure("invalid_arguments");
  cJSON *doc=load("schedules");if(!doc)return failure("local_storage_unavailable_or_corrupt");
  if(!strcmp(op,"list")){
    cJSON *out=ok(),*items=cJSON_AddArrayToObject(out,"items");unsigned offset=(unsigned)num(args,"offset"),total=cJSON_GetArraySize(field(doc,"items"));
    for(unsigned i=offset;i<total&&i-offset<8;i++)cJSON_AddItemToArray(items,cJSON_Duplicate(cJSON_GetArrayItem(field(doc,"items"),i),true));
    cJSON_AddNumberToObject(out,"total",total);if(total>offset+8)cJSON_AddNumberToObject(out,"next_offset",offset+8);
    cJSON_Delete(doc);return finish(out);
  }
  if(strcmp(op,"add")){
    if(!integer(field(args,"id"),1,2147483646)){cJSON_Delete(doc);return failure("invalid_arguments");}
    cJSON *r=find(doc,(unsigned)num(args,"id"));if(!r){cJSON_Delete(doc);return failure("schedule_not_found");}
    if(!strcmp(str(r,"state"),"executing")){cJSON_Delete(doc);return failure("schedule_already_executing_do_not_replay");}
    if(!strcmp(op,"delete")){cJSON_DetachItemViaPointer((cJSON *)field(doc,"items"),r);cJSON_Delete(r);return saved_result("schedules",doc,NULL);}
    set_bool(r,"active",false);set_string(r,"state","cancelled");return saved_result("schedules",doc,r);
  }
  if(!identifier(field(args,"request_id"))||!text(field(args,"title"),96,false,false)||
     !glass_assistant_action_valid(str(args,"action"),field(args,"arguments"))){cJSON_Delete(doc);return failure("invalid_arguments");}
  unsigned times=(field(args,"delay_seconds")!=NULL)+(field(args,"due_at")!=NULL)+(field(args,"daily_at")!=NULL);
  if(times!=1||(field(args,"delay_seconds")&&!integer(field(args,"delay_seconds"),1,31536000))||
     (field(args,"due_at")&&!integer(field(args,"due_at"),ASSISTANT_TIME_MIN,ASSISTANT_TIME_MAX))||
     (field(args,"timezone_offset_minutes")&&(!field(args,"daily_at")||!integer(field(args,"timezone_offset_minutes"),-720,840)))){cJSON_Delete(doc);return failure("invalid_arguments");}
  for(cJSON *r=field(doc,"items")->child;r;r=r->next)if(!strcmp(str(r,"request_id"),str(args,"request_id"))){
    bool same=!strcmp(str(r,"title"),str(args,"title"))&&!strcmp(str(r,"action"),str(args,"action"))&&cJSON_Compare(field(r,"arguments"),field(args,"arguments"),true);
    const char *spec[]={"delay_seconds","daily_at"};
    for(unsigned i=0;i<2;i++)same&=(field(args,spec[i])||field(r,spec[i]))?cJSON_Compare(field(r,spec[i]),field(args,spec[i]),true):true;
    if(field(args,"due_at"))same&=num(args,"due_at")==num(r,"initial_due_at");
    if(field(args,"daily_at"))same&=(field(args,"timezone_offset_minutes")?num(args,"timezone_offset_minutes"):480)==num(r,"timezone_offset_minutes");
    cJSON *out=same?ok():NULL;if(out){cJSON_AddBoolToObject(out,"already_saved",true);cJSON_AddItemToObject(out,"item",cJSON_Duplicate(r,true));}
    cJSON_Delete(doc);return same?finish(out):failure("request_id_conflict");
  }
  time_t now=time(NULL);if(now<ASSISTANT_TIME_MIN||now>ASSISTANT_TIME_MAX){cJSON_Delete(doc);return failure("clock_not_set");}
  double due=num(args,"due_at");
  if(field(args,"delay_seconds"))due=(double)now+num(args,"delay_seconds");
  if(field(args,"daily_at")){
    const char *at=str(args,"daily_at");
    if(strlen(at)!=5||at[2]!=':'||at[0]<'0'||at[0]>'2'||at[1]<'0'||at[1]>'9'||atoi(at)>23||at[3]<'0'||at[3]>'5'||at[4]<'0'||at[4]>'9'){cJSON_Delete(doc);return failure("invalid_arguments");}
    int offset=field(args,"timezone_offset_minutes")?(int)num(args,"timezone_offset_minutes"):480;
    int64_t shifted=(int64_t)now+offset*60;
    due=(double)(shifted/86400*86400-offset*60+atoi(at)*3600+atoi(at+3)*60);if(due<=now)due+=86400;
  }
  if(due<=now||due>now+31536000.0||due>ASSISTANT_TIME_MAX){cJSON_Delete(doc);return failure("schedule_time_out_of_range");}
  if(cJSON_GetArraySize(field(doc,"items"))>=ITEMS_MAX||num(doc,"next_id")>=2147483647){cJSON_Delete(doc);return failure("local_record_limit");}
  if(!scheduler_started){cJSON_Delete(doc);return failure("scheduler_unavailable");}
  cJSON *r=cJSON_CreateObject();cJSON_AddNumberToObject(r,"id",num(doc,"next_id"));set_number(doc,"next_id",num(doc,"next_id")+1);
  const char *copy[]={"request_id","title","action","arguments","delay_seconds","daily_at","timezone_offset_minutes"};
  for(unsigned i=0;i<7;i++)if(field(args,copy[i]))cJSON_AddItemToObject(r,copy[i],cJSON_Duplicate(field(args,copy[i]),true));
  if(field(args,"daily_at")&&!field(r,"timezone_offset_minutes"))cJSON_AddNumberToObject(r,"timezone_offset_minutes",480);
  cJSON_AddNumberToObject(r,"due_at",due);cJSON_AddNumberToObject(r,"initial_due_at",due);cJSON_AddBoolToObject(r,"active",true);cJSON_AddStringToObject(r,"state","pending");
  cJSON_AddItemToArray((cJSON *)field(doc,"items"),r);return saved_result("schedules",doc,r);
}

int glass_assistant_notice(const char *text)
{
  pthread_mutex_lock(&notice_lock);
  if(!notices)notices=calloc(8,sizeof(*notices));
  if(!notices){pthread_mutex_unlock(&notice_lock);return -ENOMEM;}
  if(notice_count==8){notice_first=(notice_first+1)%8;notice_count--;}
  glass_chat_copy_utf8(notices[(notice_first+notice_count)%8],sizeof(notices[0]),text);notice_count++;
  pthread_mutex_unlock(&notice_lock);return 0;
}
bool glass_assistant_take_notice(char *out,size_t cap)
{
  if(!out||!cap)return false;
  pthread_mutex_lock(&notice_lock);bool ready=notice_count!=0;
  if(ready){glass_chat_copy_utf8(out,cap,notices[notice_first]);notice_first=(notice_first+1)%8;notice_count--;}
  if(!notice_count){free(notices);notices=NULL;notice_first=0;}
  pthread_mutex_unlock(&notice_lock);return ready;
}
static void next_run(cJSON *r,time_t now)
{
  if(field(r,"daily_at")){
    double due=num(r,"due_at");due+=fmax(1,floor(((double)now-due)/86400)+1)*86400;
    set_number(r,"due_at",due);
  }else set_bool(r,"active",false);
}
int glass_assistant_tick(time_t now)
{
  if(now<ASSISTANT_TIME_MIN||now>ASSISTANT_TIME_MAX)return -EAGAIN;
  if(pthread_mutex_trylock(&tick_lock))return -EBUSY;
  int ret=0;const char *job_root=NULL;
  /* One effect per iteration keeps each execution bounded and cancellable. */
  pthread_mutex_lock(&data_lock);
  ret=select_data_root();if(ret){pthread_mutex_unlock(&data_lock);pthread_mutex_unlock(&tick_lock);return ret;}
  if(schedule_next_scan>(double)now){pthread_mutex_unlock(&data_lock);pthread_mutex_unlock(&tick_lock);return 0;}
  cJSON *doc=load("schedules"),*selected=NULL,*job=NULL;
  if(!doc){ret=-EIO;goto unlock;}
  bool dirty=false;
  for(cJSON *r=field(doc,"items")->child;r;r=r->next){
    if(!strcmp(str(r,"state"),"executing")){
      set_string(r,"state","interrupted");next_run(r,now);dirty=true;continue;
    }
    if(!cJSON_IsTrue(field(r,"active"))||num(r,"due_at")>now)continue;
    if((double)now-num(r,"due_at")>120){set_string(r,"state","missed");next_run(r,now);dirty=true;continue;}
    if(!selected)selected=r;
  }
  if(selected){set_string(selected,"state","executing");set_number(selected,"last_run",(double)now);job=cJSON_Duplicate(selected,true);job_root=data_root;dirty=true;}
  if(dirty)ret=save("schedules",doc);
  if(!ret&&!selected){
    schedule_next_scan=ASSISTANT_TIME_MAX+1.0;
    for(cJSON *r=field(doc,"items")->child;r;r=r->next)
      if(cJSON_IsTrue(field(r,"active"))&&num(r,"due_at")<schedule_next_scan)schedule_next_scan=num(r,"due_at");
  }
unlock:
  cJSON_Delete(doc);pthread_mutex_unlock(&data_lock);
  if(!ret&&job){
    atomic_bool cancel=false;
    char *response=glass_assistant_action(str(job,"action"),field(job,"arguments"),&cancel);
    cJSON *result=response?glass_agent_json(response):NULL;free(response);
    bool success=result&&cJSON_IsTrue(field(result,"ok"))&&!field(result,"error");
    pthread_mutex_lock(&data_lock);doc=load("schedules");
    if(strcmp(data_root,job_root)){cJSON_Delete(doc);doc=NULL;}
    cJSON *r=doc?find(doc,(unsigned)num(job,"id")):NULL;
    if(!r||strcmp(str(r,"request_id"),str(job,"request_id"))){ret=-EIO;}else{
      set_string(r,"state",success?"done":"failed");next_run(r,now);
      cJSON_DeleteItemFromObjectCaseSensitive(r,"result");
      /* Result payload is bounded and stripped of service-provided secrets. */
      cJSON *summary=cJSON_CreateObject();cJSON_AddBoolToObject(summary,"ok",success);
      cJSON_AddBoolToObject(summary,"verified",success&&cJSON_IsTrue(field(result,"verified")));
      cJSON_AddBoolToObject(summary,"accepted",success&&cJSON_IsTrue(field(result,"accepted")));
      if(text(field(result,"error"),96,false,false))cJSON_AddStringToObject(summary,"error",str(result,"error"));
      if(field(result,"speech_request"))cJSON_AddNumberToObject(summary,"speech_request",num(result,"speech_request"));
      cJSON_AddItemToObject(r,"result",summary);ret=save("schedules",doc);
    }
    cJSON_Delete(doc);cJSON_Delete(result);pthread_mutex_unlock(&data_lock);
  }
  cJSON_Delete(job);pthread_mutex_unlock(&tick_lock);return ret;
}
static void *scheduler_worker(void *unused)
{
  (void)unused;
  for(;;){int ret=glass_assistant_tick(time(NULL));pthread_mutex_lock(&data_lock);scheduler_error=ret;pthread_mutex_unlock(&data_lock);sleep(1);}
  return NULL;
}
int glass_assistant_start(void)
{
  pthread_mutex_lock(&data_lock);if(scheduler_started){pthread_mutex_unlock(&data_lock);return 0;}
#ifdef ASSISTANT_MANUAL_TICK
  int ret=0;
#else
  pthread_t thread;pthread_attr_t attr;int ret=pthread_attr_init(&attr);
  if(!ret){ret=pthread_attr_setstacksize(&attr,32768);if(!ret)ret=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if(!ret)ret=pthread_create(&thread,&attr,scheduler_worker,NULL);
    pthread_attr_destroy(&attr);}
#endif
  scheduler_started=!ret;scheduler_error=-ret;pthread_mutex_unlock(&data_lock);return -ret;
}
static char *call(const char *tool,const cJSON *args,atomic_bool *cancel)
{
  if(cancel&&atomic_load(cancel))return failure("cancelled");
  if(!cJSON_IsObject(args))return failure("invalid_arguments");
  if(!strcmp(tool,"assistant_status")){
    if(!only(args,""))return failure("invalid_arguments");
    char *platform=glass_assistant_platform(tool,args,cancel);cJSON *out=platform?glass_agent_json(platform):ok();free(platform);
    if(!out)return failure("platform_unavailable");
    time_t now=time(NULL);cJSON_AddNumberToObject(out,"now",(double)now);
    cJSON_AddBoolToObject(out,"clock_valid",now>=ASSISTANT_TIME_MIN&&now<=ASSISTANT_TIME_MAX);cJSON_AddNumberToObject(out,"timezone_offset_minutes",480);
    struct tm utc;char iso[32]="";if(gmtime_r(&now,&utc))strftime(iso,sizeof(iso),"%Y-%m-%dT%H:%M:%SZ",&utc);cJSON_AddStringToObject(out,"utc",iso);
    pthread_mutex_lock(&data_lock);cJSON_AddBoolToObject(out,"scheduler_running",scheduler_started);cJSON_AddNumberToObject(out,"scheduler_error",scheduler_error);
    cJSON *stores=cJSON_AddObjectToObject(out,"stores");const char *kinds[]={"note","todo","event","schedules","aliases"};
    for(unsigned i=0;i<5;i++){cJSON *doc=load(kinds[i]);cJSON_AddNumberToObject(stores,kinds[i],doc?cJSON_GetArraySize(field(doc,"items")):-1);cJSON_Delete(doc);}
    pthread_mutex_unlock(&data_lock);return finish(out);
  }
  if(!strcmp(tool,"assistant_records")||!strcmp(tool,"assistant_schedule")||!strcmp(tool,"assistant_alias")){
    pthread_mutex_lock(&data_lock);
    char *out=!strcmp(tool,"assistant_records")?records(args):!strcmp(tool,"assistant_schedule")?schedules(args):aliases(args);
    pthread_mutex_unlock(&data_lock);return out;
  }
  return glass_assistant_platform(tool,args,cancel);
}
static const struct glass_board_home provider={assistant_tool_catalog,call};
void glass_board_assistant_register(void){glass_agent_set_board_assistant(&provider);}
