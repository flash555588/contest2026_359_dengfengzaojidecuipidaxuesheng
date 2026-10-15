/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_agent.h"
#include "glass_https.h"
#include <netutils/webclient.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
struct mcp_wire {
  struct glass_mcp_http *request;
  bool sse,matched;
  size_t used,line_used,event_used,total;
  char json[AGENT_TOOL_BYTES+1],line[AGENT_TOOL_BYTES+1],event[AGENT_TOOL_BYTES+1],buffer[2048];
};
static int mcp_message(struct mcp_wire *w,const char *text)
{
  cJSON *root=glass_agent_json(text);if(!root)return -EPROTO;
  for(cJSON *v=root->child;v;v=v->next)for(cJSON *next=v->next;next;next=next->next)
    if(v->string&&next->string&&!strcmp(v->string,next->string)){cJSON_Delete(root);return -EPROTO;}
  cJSON *version=cJSON_GetObjectItemCaseSensitive(root,"jsonrpc"),*id=cJSON_GetObjectItemCaseSensitive(root,"id");
  if(!cJSON_IsObject(root)||!cJSON_IsString(version)||strcmp(version->valuestring,"2.0")){cJSON_Delete(root);return -EPROTO;}
  if(cJSON_GetObjectItemCaseSensitive(root,"method")){
    /* No sampling/elicitation capabilities advertised. Ignore notifications;
     * unsupported server-to-client requests fail explicitly. */
    int ret=id?-ENOTSUP:0;cJSON_Delete(root);return ret;
  }
  if(!cJSON_IsNumber(id)||id->valuedouble!=w->request->response_id){cJSON_Delete(root);return -EPROTO;}
  cJSON *result=cJSON_GetObjectItemCaseSensitive(root,"result"),*error=cJSON_GetObjectItemCaseSensitive(root,"error");
  if((!result&&!error)||(result&&error)){cJSON_Delete(root);return -EPROTO;}
  w->request->result=root;w->matched=true;return -EALREADY;
}
static int mcp_sink(char **buffer,int offset,int end,int *length,void *opaque)
{
  struct mcp_wire *w=opaque;
  if(w->request->cancel&&atomic_load(w->request->cancel))return -ECANCELED;
  if(offset<0||end<offset||end>*length)return -EINVAL;
  if(w->total+(unsigned)(end-offset)>AGENT_TOOL_BYTES*8)return -EFBIG;
  w->total+=end-offset;
  if(!w->sse){
    if(memchr(*buffer+offset,0,end-offset))return -EPROTO;
    if((unsigned)(end-offset)>AGENT_TOOL_BYTES-w->used)return -EFBIG;
    memcpy(w->json+w->used,*buffer+offset,end-offset);w->used+=end-offset;w->json[w->used]=0;return 0;
  }
  for(int i=offset;i<end;i++){
    char c=(*buffer)[i];if(c=='\r')continue;
    if(!c)return -EPROTO;
    if(c!='\n'){if(w->line_used==AGENT_TOOL_BYTES)return -EFBIG;w->line[w->line_used++]=c;continue;}
    w->line[w->line_used]=0;
    if(!w->line_used){
      if(w->event_used){w->event[w->event_used]=0;int ret=mcp_message(w,w->event);if(ret)return ret;}
      w->event_used=0;
    }else if(!strncmp(w->line,"data:",5)){
      const char *data=w->line+5;if(*data==' ')data++;
      size_t n=strlen(data);if(n+w->event_used+1>AGENT_TOOL_BYTES)return -EFBIG;
      if(w->event_used)w->event[w->event_used++]='\n';
      memcpy(w->event+w->event_used,data,n);w->event_used+=n;
    }
    w->line_used=0;
  }
  return 0;
}
static int mcp_header(const char *line,bool truncated,void *opaque)
{
  struct mcp_wire *w=opaque;if(truncated)return -EFBIG;
  if(!strncasecmp(line,"location:",9))return -EACCES;
  if(!strncasecmp(line,"content-encoding:",17)&&!strstr(line,"identity"))return -ENOTSUP;
  if(!strncasecmp(line,"content-type:",13)){
    const char *value=line+13;while(*value==' ')value++;
    w->sse=!strncasecmp(value,"text/event-stream",17);
    if(!w->sse&&strncasecmp(value,"application/json",16))return -EPROTO;
  }
  if(!strncasecmp(line,"mcp-session-id:",15)){
    const char *value=line+15;while(*value==' ')value++;
    size_t n=strcspn(value,"\r\n");if(!n||n>128)return -EFBIG;
    for(size_t i=0;i<n;i++)if((unsigned char)value[i]<33||(unsigned char)value[i]>126)return -EPROTO;
    memcpy(w->request->response_session,value,n);w->request->response_session[n]=0;
  }
  return 0;
}
int glass_mcp_http_perform(struct glass_mcp_http *request)
{
  request->result=NULL;request->status=0;request->response_session[0]=0;
  if(request->cancel&&atomic_load(request->cancel))return -ECANCELED;
  int ret=glass_https_initialize();if(ret)return ret;
  struct mcp_wire *wire=calloc(1,sizeof(*wire));if(!wire)return -ENOMEM;wire->request=request;
  char auth[544],session[160],version[64];
  const char *headers[8]={"Content-Type: application/json","Accept: application/json, text/event-stream","Accept-Encoding: identity"};unsigned count=3;
  if(request->token&&request->token[0]){snprintf(auth,sizeof(auth),"Authorization: Bearer %s",request->token);headers[count++]=auth;}
  if(request->session&&request->session[0]){snprintf(session,sizeof(session),"MCP-Session-Id: %s",request->session);headers[count++]=session;}
  if(request->version&&request->version[0]){snprintf(version,sizeof(version),"MCP-Protocol-Version: %s",request->version);headers[count++]=version;}
  size_t ca_size;const unsigned char *ca=glass_agent_trust(&ca_size);
  struct glass_https_request tls={.ca_pem=ca,.ca_size=ca_size,
    .abort_flag=request->cancel,.deadline_ms=glass_https_milliseconds()+request->timeout_ms};
  struct webclient_context client;webclient_set_defaults(&client);
  client.url=request->url;client.method=request->method;client.headers=headers;client.nheaders=count;
  client.protocol_version=WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;client.timeout_sec=(request->timeout_ms+999)/1000;
  client.buffer=wire->buffer;client.buflen=sizeof(wire->buffer);client.tls_ops=glass_https_tls_ops();client.tls_ctx=&tls;
  client.sink_callback=mcp_sink;client.sink_callback_arg=wire;client.header_callback=mcp_header;client.header_callback_arg=wire;
  if(request->body)webclient_set_static_body(&client,request->body,strlen(request->body));
  ret=webclient_perform(&client);request->status=client.http_status;
  if(ret==-EALREADY&&wire->matched)ret=0;
  if(!ret&&request->status==200&&request->response_id&&!wire->matched){
    ret=wire->sse?-EPROTO:mcp_message(wire,wire->json);if(ret==-EALREADY)ret=0;
  }
  if(request->cancel&&atomic_load(request->cancel))ret=-ECANCELED;
  memset(auth,0,sizeof(auth));free(wire);return ret;
}
