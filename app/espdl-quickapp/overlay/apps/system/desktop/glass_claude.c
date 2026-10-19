/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_claude.h"
#include "glass_agent.h"
#include <netutils/webclient.h>
#include <cJSON.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define CLAUDE_RESPONSE 16384
#define CLAUDE_BODY 4096
struct claude_job {
  unsigned id; bool done,abandoned; time_t completed;
  char url[112],token[65],body[CLAUDE_BODY+1];
  char *result;
};
struct claude_wire { char body[CLAUDE_RESPONSE+1],buffer[2048]; size_t used; };
static pthread_mutex_t claude_lock=PTHREAD_MUTEX_INITIALIZER;
static struct claude_job *claude_job;
static unsigned claude_sequence;

static bool claude_base_valid(const char *base)
{
  unsigned a,b,c,d,port;char rest;
  if(!base||sscanf(base,"http://%u.%u.%u.%u:%u%c",&a,&b,&c,&d,&port,&rest)!=5||
     a>255||b>255||c>255||d>255||!port||port>65535||
     !(a==10||(a==172&&b>=16&&b<=31)||(a==192&&b==168)))return false;
  char normalized[80];snprintf(normalized,sizeof(normalized),"http://%u.%u.%u.%u:%u",a,b,c,d,port);
  return !strcmp(base,normalized);
}
static int claude_sink(char **buffer,int offset,int end,int *length,void *opaque)
{
  struct claude_wire *w=opaque;
  if(!buffer||!*buffer||!length||offset<0||end<offset||end>*length)return -EINVAL;
  size_t size=end-offset;
  if(size>CLAUDE_RESPONSE-w->used)return -EFBIG;
  if(memchr(*buffer+offset,0,size))return -EPROTO;
  memcpy(w->body+w->used,*buffer+offset,size);w->used+=size;w->body[w->used]=0;return 0;
}
static int claude_header(const char *line,bool truncated,void *opaque)
{
  (void)opaque;if(truncated)return -EFBIG;
  if(!strncasecmp(line,"location:",9))return -EACCES;
  if(!strncasecmp(line,"content-encoding:",17)&&!strstr(line,"identity"))return -ENOTSUP;
  return 0;
}
static void *claude_worker(void *opaque)
{
  struct claude_job *job=opaque;
  struct claude_wire *wire=calloc(1,sizeof(*wire));
  int error=-ENOMEM,status=0;
  if(wire){
    char auth[96];snprintf(auth,sizeof(auth),"Authorization: Bearer %s",job->token);
    const char *headers[]={"Content-Type: application/json","Accept: application/json","Accept-Encoding: identity",auth};
    struct webclient_context client;webclient_set_defaults(&client);
    client.url=job->url;client.method="POST";client.headers=headers;client.nheaders=4;
    client.protocol_version=WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;client.timeout_sec=5;
    client.buffer=wire->buffer;client.buflen=sizeof(wire->buffer);
    client.sink_callback=claude_sink;client.sink_callback_arg=wire;
    client.header_callback=claude_header;
    webclient_set_static_body(&client,job->body,strlen(job->body));
    error=webclient_perform(&client);status=client.http_status;
    memset(auth,0,sizeof(auth));
  }
  cJSON *result=cJSON_CreateObject();
  if(result){
    cJSON_AddBoolToObject(result,"done",true);cJSON_AddNumberToObject(result,"error",error);
    cJSON_AddNumberToObject(result,"status",status);
    cJSON *body=!error&&wire?glass_agent_json(wire->body):NULL;
    if(body)cJSON_AddItemToObject(result,"body",body);
    else if(!error)cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(result,"error"),-EPROTO);
  }
  char *printed=result?cJSON_PrintUnformatted(result):NULL;cJSON_Delete(result);free(wire);
  pthread_mutex_lock(&claude_lock);
  memset(job->token,0,sizeof(job->token));memset(job->body,0,sizeof(job->body));
  job->result=printed;job->done=true;job->completed=time(NULL);
  if(job->abandoned){free(job->result);memset(job,0,sizeof(*job));free(job);claude_job=NULL;}
  pthread_mutex_unlock(&claude_lock);return NULL;
}
int glass_claude_request(const char *base,const char *token,const char *action,const char *json)
{
  if(!claude_base_valid(base)||!token||!action||!json||strlen(json)>CLAUDE_BODY)return -EINVAL;
  if(strcmp(action,"pair")&&strcmp(action,"status")&&strcmp(action,"project")&&strcmp(action,"submit")&&strcmp(action,"stop")&&strcmp(action,"approve")&&strcmp(action,"input"))return -EINVAL;
  size_t n=strlen(token);
  if((strcmp(action,"pair")&&n!=64)||(n&&n!=64))return -EINVAL;
  for(size_t i=0;i<n;i++)if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f')))return -EINVAL;
  cJSON *parsed=glass_agent_json(json);bool valid=cJSON_IsObject(parsed);cJSON_Delete(parsed);if(!valid)return -EINVAL;
  pthread_mutex_lock(&claude_lock);
  if(claude_job&&claude_job->done&&time(NULL)-claude_job->completed>30){free(claude_job->result);free(claude_job);claude_job=NULL;}
  if(claude_job){pthread_mutex_unlock(&claude_lock);return -EBUSY;}
  if(claude_sequence==INT_MAX){pthread_mutex_unlock(&claude_lock);return -EOVERFLOW;}
  struct claude_job *job=calloc(1,sizeof(*job));
  if(!job){pthread_mutex_unlock(&claude_lock);return -ENOMEM;}
  job->id=++claude_sequence;snprintf(job->url,sizeof(job->url),"%s/api/%s",base,action);
  strcpy(job->token,token);strcpy(job->body,json);claude_job=job;
  pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,16384);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);pthread_t thread;
  int ret=pthread_create(&thread,&attr,claude_worker,job);pthread_attr_destroy(&attr);
  if(ret){memset(job,0,sizeof(*job));free(job);claude_job=NULL;}
  int id=ret?-ret:(int)job->id;pthread_mutex_unlock(&claude_lock);return id;
}
char *glass_claude_poll(unsigned id)
{
  pthread_mutex_lock(&claude_lock);char *result;
  if(!claude_job||claude_job->id!=id)result=strdup("{\"done\":true,\"error\":-2}");
  else if(!claude_job->done)result=strdup("{\"done\":false}");
  else{
    result=claude_job->result;claude_job->result=NULL;free(claude_job);claude_job=NULL;
    if(!result)result=strdup("{\"done\":true,\"error\":-12}");
  }
  pthread_mutex_unlock(&claude_lock);return result;
}
void glass_claude_abandon(void)
{
  pthread_mutex_lock(&claude_lock);
  if(claude_job){
    if(claude_job->done){free(claude_job->result);memset(claude_job,0,sizeof(*claude_job));free(claude_job);claude_job=NULL;}
    else claude_job->abandoned=true;
  }
  pthread_mutex_unlock(&claude_lock);
}
