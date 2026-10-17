/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_LOCAL_VOICE_H
#define GLASS_LOCAL_VOICE_H
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"
/* This integration supports individual lights, curtains and fans only. */
static bool local_voice_entity(const char *id,int role)
{
  const char *domains[]={"light.","cover.","fan."};
  if(!id||role<0||role>2||strlen(id)>=96)return false;
  size_t prefix=strlen(domains[role]);
  if(strncmp(id,domains[role],prefix)||!id[prefix])return false;
  for(const char *p=id+prefix;*p;p++)if((*p<'a'||*p>'z')&&(*p<'0'||*p>'9')&&*p!='_')return false;
  return true;
}
static const char *local_voice_string(const cJSON *o,const char *key)
{const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)?v->valuestring:"";}
static bool local_voice_allowed(const cJSON *device,int role,const char *expected)
{
  const char *id=local_voice_string(device,"entity_id");
  if(!local_voice_entity(id,role)||(expected&&strcmp(id,expected)))return false;
  const char *state=local_voice_string(device,"state");
  if(role!=1)return !strcmp(state,"on")||!strcmp(state,"off");
  const cJSON *attr=cJSON_GetObjectItemCaseSensitive(device,"attributes");
  const char *kind=local_voice_string(attr,"device_class");
  if(strcmp(kind,"curtain")&&strcmp(kind,"blind")&&strcmp(kind,"shade")&&strcmp(kind,"shutter"))return false;
  return !strcmp(state,"open")||!strcmp(state,"closed")||!strcmp(state,"opening")||!strcmp(state,"closing");
}
static bool local_voice_matches(const cJSON *device,int role,bool on)
{
  const char *state=local_voice_string(device,"state");
  if(role!=1)return !strcmp(state,on?"on":"off");
  if(strcmp(state,on?"open":"closed"))return false;
  const cJSON *attr=cJSON_GetObjectItemCaseSensitive(device,"attributes");
  const cJSON *position=cJSON_GetObjectItemCaseSensitive(attr,"current_position");
  return !position||(cJSON_IsNumber(position)&&isfinite(position->valuedouble)&&position->valuedouble==(on?100:0));
}
/* Reject oversized/deep input before recursive JSON parsing on the UI stack. */
static cJSON *local_voice_json(const char *text)
{
  if(!text)return NULL;
  unsigned depth=0;bool quoted=false,escape=false;size_t bytes=0;
  for(const char *p=text;*p;p++){
    if(++bytes>512*1024)return NULL;
    if(escape){escape=false;continue;}
    if(quoted&&*p=='\\'){escape=true;continue;}
    if(*p=='"'){quoted=!quoted;continue;}
    if(!quoted&&(*p=='{'||*p=='[')&&++depth>16)return NULL;
    if(!quoted&&(*p=='}'||*p==']')){if(!depth)return NULL;depth--;}
  }
  return depth||quoted?NULL:cJSON_ParseWithOpts(text,NULL,true);
}
#endif
