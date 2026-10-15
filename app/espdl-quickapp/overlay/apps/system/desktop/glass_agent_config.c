/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_agent.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
cJSON *glass_agent_json(const char *text)
{
  if(!text||strlen(text)>AGENT_TOOL_BYTES||strstr(text,"\\u0000"))return NULL;
  unsigned depth=0;bool quoted=false,escape=false;
  for(const char *p=text;*p;p++){
    if(escape){escape=false;continue;}
    if(quoted&&*p=='\\'){escape=true;continue;}
    if(*p=='"'){quoted=!quoted;continue;}
    if(!quoted&&(*p=='{'||*p=='[')&&++depth>16)return NULL;
    if(!quoted&&(*p=='}'||*p==']')){if(!depth)return NULL;depth--;}
  }
  return quoted||depth?NULL:cJSON_ParseWithOpts(text,NULL,true);
}
bool glass_agent_field(const char *key,unsigned *limit,bool *secret,bool *number)
{
  *limit=0;*secret=*number=false;
  if(!strcmp(key,"skills_enabled")){*number=true;return true;}
  if(strncmp(key,"mcp",3)||key[3]<'1'||key[3]>'3'||key[4]!='_')return false;
  const char *field=key+5;
  if(!strcmp(field,"enabled"))*number=true;
  else if(!strcmp(field,"name"))*limit=48;
  else if(!strcmp(field,"url"))*limit=512;
  else if(!strcmp(field,"token")){*limit=512;*secret=true;}
  else if(!strcmp(field,"tools"))*limit=512;
  else return false;
  return true;
}
static bool identifier(const char *s)
{if(!*s)return false;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='_'&&*s!='-'&&*s!='.')return false;return true;}
bool glass_agent_value_valid(const char *key,const cJSON *value)
{
  unsigned limit;bool secret,number;
  if(!glass_agent_field(key,&limit,&secret,&number))return false;
  if(number)return cJSON_IsNumber(value)&&(value->valuedouble==0||value->valuedouble==1);
  if(!cJSON_IsString(value)||strlen(value->valuestring)>limit)return false;
  const char *s=value->valuestring;
  for(const unsigned char *p=(const unsigned char *)s;*p;p++)if(*p<32||*p==127)return false;
  if(!strcmp(key+5,"url")&&*s){
    if(strncmp(s,"https://",8)||!s[8]||s[8]=='/'||strpbrk(s+8,"@#\\ \t\r\n"))return false;
  }
  if(!strcmp(key+5,"tools")&&*s){
    if(!strcmp(s,"*"))return true;
    char copy[513];strcpy(copy,s);char *save=NULL;
    for(char *part=strtok_r(copy,",",&save);part;part=strtok_r(NULL,",",&save))if(!identifier(part))return false;
    if(s[0]==','||s[strlen(s)-1]==','||strstr(s,",,"))return false;
  }
  return true;
}
