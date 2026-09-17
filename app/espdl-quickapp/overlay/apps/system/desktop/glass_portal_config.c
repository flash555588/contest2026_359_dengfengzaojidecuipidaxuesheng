/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_portal.h"
#include "qpk_storage.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef PORTAL_CONFIG_ROOT
#define PORTAL_CONFIG_ROOT "/data/config"
#endif
static pthread_mutex_t config_lock=PTHREAD_MUTEX_INITIALIZER;
static uint8_t desktop_values[4],desktop_pending[4];
static bool desktop_dirty;
static cJSON *load(const char *section)
{
  char *data=malloc(8193); size_t size=0; cJSON *obj=NULL;
  if(!data) return NULL;
  int ret=qpk_storage_read(PORTAL_CONFIG_ROOT,"portal",section,data,8193,&size);
  if(!ret) {
    data[size]=0; obj=cJSON_ParseWithOpts(data,NULL,true);
  }
  free(data);
  if(ret==-ENOENT) obj=cJSON_CreateObject();
  if(!cJSON_IsObject(obj)) { cJSON_Delete(obj); return NULL; }
  return obj;
}
static const char *string(const cJSON *obj,const char *key,const char *fallback)
{ cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key); return cJSON_IsString(v)?v->valuestring:fallback; }
static unsigned number(const cJSON *obj,const char *key,unsigned fallback)
{ cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key); return cJSON_IsNumber(v)?(unsigned)v->valuedouble:fallback; }
static void default_string(cJSON *o,const char *key,const char *value)
{ if(!cJSON_GetObjectItemCaseSensitive(o,key)) cJSON_AddStringToObject(o,key,value); }
static cJSON *defaults(const char *section)
{
  cJSON *o=load(section); if(!o) return NULL;
  if(!strcmp(section,"ai")) {
    default_string(o,"base_url","https://api.openai.com/v1"); default_string(o,"model","");
    default_string(o,"backend","openai_compatible"); default_string(o,"api_key","");
    default_string(o,"system_prompt","请准确、简洁地回答用户，使用用户的语言。");
    if(!cJSON_GetObjectItemCaseSensitive(o,"max_tokens")) cJSON_AddNumberToObject(o,"max_tokens",16384);
    if(!cJSON_GetObjectItemCaseSensitive(o,"timeout_ms")) cJSON_AddNumberToObject(o,"timeout_ms",300000);
  } else if(!strcmp(section,"homeassistant")) {
    char *legacy=malloc(8193); size_t length;
    if(legacy) {
      if(!cJSON_GetObjectItemCaseSensitive(o,"url")&&!qpk_storage_read("/data/qpk","com.openvela.homeassistant","ha_url",legacy,8193,&length)) default_string(o,"url",legacy);
      if(!cJSON_GetObjectItemCaseSensitive(o,"token")&&!qpk_storage_read("/data/qpk","com.openvela.homeassistant","ha_token",legacy,8193,&length)) default_string(o,"token",legacy);
      free(legacy);
    }
    default_string(o,"url","http://homeassistant.local:8123"); default_string(o,"token","");
  } else if(!strcmp(section,"music")) default_string(o,"api_key","");
  else if(!strcmp(section,"voice")) {
    default_string(o,"app_id",""); default_string(o,"secret_id","");
    default_string(o,"secret_key","");
  }
  else if(!strcmp(section,"desktop")) {
    cJSON_Delete(o); o=cJSON_CreateObject();
    const char *keys[]={"light","palette","widget","reduced_motion"};
    for(unsigned i=0;i<4;i++) cJSON_AddNumberToObject(o,keys[i],desktop_values[i]);
  } else { cJSON_Delete(o); return NULL; }
  return o;
}
static bool valid_url(const char *s,bool https)
{
  if(!*s) return true;
  const char *host=!strncmp(s,"https://",8)?s+8:!https&&!strncmp(s,"http://",7)?s+7:NULL;
  if(!host || !*host || *host=='/' || strpbrk(host,"@?#\\ \t\r\n")) return false;
  return true;
}
struct field { const char *section,*key; unsigned limit; bool secret; };
static const struct field fields[]={
 {"ai","api_key",2048,true},{"ai","base_url",1024,false},{"ai","model",128,false},
 {"ai","backend",32,false},{"ai","system_prompt",1024,false},
 {"homeassistant","url",1024,false},{"homeassistant","token",2048,true},{"music","api_key",128,true},
 {"voice","app_id",32,false},{"voice","secret_id",128,true},{"voice","secret_key",128,true}
};
cJSON *portal_config_get(const char *section)
{
  pthread_mutex_lock(&config_lock); cJSON *o=defaults(section);
  if(o) for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)
    if(fields[i].secret && !strcmp(section,fields[i].section)) {
      bool present=string(o,fields[i].key,"")[0]!=0;
      cJSON_DeleteItemFromObjectCaseSensitive(o,fields[i].key);
      char key[64]; snprintf(key,sizeof(key),"%s_set",fields[i].key); cJSON_AddBoolToObject(o,key,present);
    }
  pthread_mutex_unlock(&config_lock); return o;
}
int portal_config_save(const char *section,const cJSON *patch)
{
  if(!cJSON_IsObject(patch)) return -EINVAL;
  pthread_mutex_lock(&config_lock); cJSON *o=defaults(section); int ret=-EINVAL;
  if(!o) goto done;
  const char *desktop_keys[]={"light","palette","widget","reduced_motion"};
  for(const cJSON *v=patch->child;v;v=v->next) {
    if(!v->string) goto done;
    for(const cJSON *next=v->next;next;next=next->next) if(!strcmp(next->string,v->string)) goto done;
    unsigned limit=0; bool known=false;
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)
      if(!strcmp(section,fields[i].section)&&!strcmp(v->string,fields[i].key)) { limit=fields[i].limit; known=true; }
    if(known) {
      if(!cJSON_IsString(v)||strlen(v->valuestring)>limit) goto done;
      for(const unsigned char *s=(unsigned char *)v->valuestring;*s;s++)
        if((*s<32 || *s==127) && (strcmp(v->string,"system_prompt") || (*s!='\n'&&*s!='\t'))) goto done;
      if((!strcmp(v->string,"base_url")||!strcmp(v->string,"url"))&&!valid_url(v->valuestring,!strcmp(section,"ai"))) goto done;
      if(!strcmp(v->string,"backend") && strcmp(v->valuestring,"openai_compatible") && strcmp(v->valuestring,"anthropic_compatible")) goto done;
      if(!strcmp(section,"voice")&&!strcmp(v->string,"app_id")&&
         v->valuestring[0]&&strspn(v->valuestring,"0123456789")!=strlen(v->valuestring)) goto done;
    } else {
      unsigned min=0,max=0;
      if(!strcmp(section,"ai")&&!strcmp(v->string,"max_tokens")) { min=1; max=384000; }
      if(!strcmp(section,"ai")&&!strcmp(v->string,"timeout_ms")) { min=5000; max=3600000; }
      if(!strcmp(section,"desktop")) for(unsigned i=0;i<4;i++) if(!strcmp(v->string,desktop_keys[i])) max=(i==1||i==2)?2:1;
      if(!max||!cJSON_IsNumber(v)||v->valuedouble<min||v->valuedouble>max||v->valuedouble!=(int)v->valuedouble) goto done;
    }
    cJSON *copy=cJSON_Duplicate(v,true); if(!copy) { ret=-ENOMEM; goto done; }
    cJSON_DeleteItemFromObjectCaseSensitive(o,v->string); cJSON_AddItemToObject(o,v->string,copy);
  }
  if(!strcmp(section,"desktop")) {
    for(unsigned i=0;i<4;i++) desktop_pending[i]=number(o,desktop_keys[i],0);
    desktop_dirty=true; ret=0; goto done;
  }
  char *data=cJSON_PrintUnformatted(o);
  if(!data) { ret=-ENOMEM; goto done; }
  ret=qpk_storage_write(PORTAL_CONFIG_ROOT,"portal",section,data,strlen(data)); free(data);
done:
  cJSON_Delete(o); pthread_mutex_unlock(&config_lock); return ret;
}
int portal_ai_load(struct portal_ai_config *out)
{
  if(!out) return -EINVAL;
  pthread_mutex_lock(&config_lock); cJSON *o=defaults("ai"); int ret=-ENOMEM;
  if(o) {
    snprintf(out->api_key,sizeof(out->api_key),"%s",string(o,"api_key",""));
    snprintf(out->base_url,sizeof(out->base_url),"%s",string(o,"base_url",""));
    snprintf(out->model,sizeof(out->model),"%s",string(o,"model",""));
    snprintf(out->backend,sizeof(out->backend),"%s",string(o,"backend","openai_compatible"));
    snprintf(out->system_prompt,sizeof(out->system_prompt),"%s",string(o,"system_prompt",""));
    out->timeout_ms=number(o,"timeout_ms",300000); out->max_tokens=number(o,"max_tokens",16384);
    ret=out->api_key[0]&&out->model[0]&&out->base_url[0]?0:-ENOENT;
  }
  cJSON_Delete(o); pthread_mutex_unlock(&config_lock); return ret;
}
int portal_voice_load(struct portal_voice_config *out)
{
  if(!out) return -EINVAL;
  memset(out,0,sizeof(*out));
  pthread_mutex_lock(&config_lock); cJSON *o=defaults("voice"); int ret=-ENOMEM;
  if(o) {
    snprintf(out->app_id,sizeof(out->app_id),"%s",string(o,"app_id",""));
    snprintf(out->secret_id,sizeof(out->secret_id),"%s",string(o,"secret_id",""));
    snprintf(out->secret_key,sizeof(out->secret_key),"%s",string(o,"secret_key",""));
    ret=out->app_id[0]&&out->secret_id[0]&&out->secret_key[0]?0:-ENOENT;
  }
  cJSON_Delete(o); pthread_mutex_unlock(&config_lock); return ret;
}
int portal_music_key(char *out,size_t capacity)
{
  pthread_mutex_lock(&config_lock); cJSON *o=defaults("music");
  int ret=o&&string(o,"api_key","")[0]?0:-ENOENT;
  if(!ret) snprintf(out,capacity,"%s",string(o,"api_key",""));
  cJSON_Delete(o); pthread_mutex_unlock(&config_lock); return ret;
}
/* Overrides are read on app launch, without returning credentials to HTTP. */
int portal_ha_value(const char *key,char *out,size_t capacity)
{
  if(!out||!capacity) return -EINVAL;
  out[0]='\0';
  if(strcmp(key,"ha_url")&&strcmp(key,"ha_token")) return -ENOENT;
  pthread_mutex_lock(&config_lock); cJSON *o=defaults("homeassistant");
  cJSON *v=cJSON_GetObjectItemCaseSensitive(o,!strcmp(key,"ha_url")?"url":"token");
  int ret=cJSON_IsString(v)?0:-ENOENT;
  if(!ret&&strlen(v->valuestring)>=capacity) ret=-ENOSPC;
  if(!ret) strcpy(out,v->valuestring);
  cJSON_Delete(o); pthread_mutex_unlock(&config_lock); return ret;
}
int portal_ha_set(const char *key,const char *value)
{
  if(strcmp(key,"ha_url")&&strcmp(key,"ha_token")) return -ENOENT;
  cJSON *patch=cJSON_CreateObject(); if(!patch) return -ENOMEM;
  cJSON_AddStringToObject(patch,!strcmp(key,"ha_url")?"url":"token",value);
  int ret=portal_config_save("homeassistant",patch); cJSON_Delete(patch); return ret;
}
int portal_ha_save(const char *url,const char *token)
{
  if(!url||!token||strlen(url)>1024||strlen(token)>2048) return -EINVAL;
  cJSON *patch=cJSON_CreateObject(); if(!patch) return -ENOMEM;
  if(!cJSON_AddStringToObject(patch,"url",url)||
     !cJSON_AddStringToObject(patch,"token",token))
    { cJSON_Delete(patch); return -ENOMEM; }
  int ret=portal_config_save("homeassistant",patch); cJSON_Delete(patch); return ret;
}
void portal_desktop_publish(const uint8_t values[4])
{ pthread_mutex_lock(&config_lock); memcpy(desktop_values,values,4); pthread_mutex_unlock(&config_lock); }
bool portal_desktop_take(uint8_t values[4])
{ pthread_mutex_lock(&config_lock); bool ready=desktop_dirty; if(ready) { memcpy(values,desktop_pending,4); desktop_dirty=false; } pthread_mutex_unlock(&config_lock); return ready; }
