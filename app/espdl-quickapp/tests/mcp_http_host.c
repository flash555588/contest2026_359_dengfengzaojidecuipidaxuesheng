/* SPDX-License-Identifier: Apache-2.0 */
/* Real HTTP header, JSON-RPC and byte-fragmented SSE parser; no sockets. */
#include "glass_mcp_http.c"
#include <assert.h>
static const char *wire_body,*wire_type="application/json",*wire_session;
static int wire_status=200;
static bool redirect,overlong,header_truncated;
static unsigned requests;
int64_t glass_https_milliseconds(void){return 1000;}
int glass_https_initialize(void){return 0;}
const unsigned char *glass_agent_trust(size_t *size){static const unsigned char ca[]="test CA";*size=sizeof(ca);return ca;}
const struct webclient_tls_ops *glass_https_tls_ops(void){static struct webclient_tls_ops ops;return &ops;}
void webclient_set_defaults(struct webclient_context *client){memset(client,0,sizeof(*client));}
void webclient_set_static_body(struct webclient_context *client,const char *body,size_t size){(void)client;assert(body&&size);}
int webclient_perform(struct webclient_context *client)
{
  requests++;assert(!strcmp(client->method,"POST"));assert(client->nheaders==6);
  bool accept=false,version=false,session=false,token=false;
  for(unsigned i=0;i<client->nheaders;i++){
    const char *h=client->headers[i];accept|=!strcmp(h,"Accept: application/json, text/event-stream");
    version|=!strcmp(h,"MCP-Protocol-Version: 2025-11-25");session|=!strcmp(h,"MCP-Session-Id: session-1");token|=!strcmp(h,"Authorization: Bearer secret");
  }
  assert(accept&&version&&session&&token);client->http_status=wire_status;
  char header[256];snprintf(header,sizeof(header),"Content-Type: %s",wire_type);
  int ret=client->header_callback(header,header_truncated,client->header_callback_arg);if(ret)return ret;
  if(redirect)return client->header_callback("Location: http://other/",false,client->header_callback_arg);
  if(wire_session){snprintf(header,sizeof(header),"MCP-Session-Id: %s",wire_session);ret=client->header_callback(header,false,client->header_callback_arg);if(ret)return ret;}
  const char *text=wire_body;int size=1;
  for(size_t i=0;text&&i<strlen(text);i++){
    char *p=(char *)text+i;ret=client->sink_callback(&p,0,1,&size,client->sink_callback_arg);if(ret)return ret;
  }
  if(overlong){char *p="x";for(unsigned i=0;i<AGENT_TOOL_BYTES*8;i++){ret=client->sink_callback(&p,0,1,&size,client->sink_callback_arg);if(ret)return ret;}}
  return 0;
}
static int perform(const char *body,bool sse)
{
  wire_body=body;wire_type=sse?"text/event-stream":"application/json";
  struct glass_mcp_http request={.url="https://mcp.example/mcp",.token="secret",.session="session-1",.version="2025-11-25",.method="POST",.body="{}",.timeout_ms=1000,.response_id=7};
  int ret=glass_mcp_http_perform(&request);cJSON_Delete(request.result);return ret;
}
int main(void)
{
  assert(!perform("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"tools\":[]}}",false));
  assert(perform("{\"jsonrpc\":\"2.0\",\"id\":8,\"result\":{}}",false)==-EPROTO);
  assert(perform("{\"jsonrpc\":\"2.0\",\"id\":7,\"id\":8,\"result\":{}}",false)==-EPROTO);
  char nested[100];memset(nested,'[',30);memset(nested+30,']',30);nested[60]=0;assert(!glass_agent_json(nested));
  assert(perform("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{},\"error\":{}}",false)==-EPROTO);
  assert(!perform(": heartbeat\r\n\r\ndata:\r\n\r\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\r\n\r\ndata: {\"jsonrpc\":\"2.0\",\r\ndata: \"id\":7,\"result\":{\"content\":[]}}\r\n\r\n",true));
  assert(perform("data: {\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"sampling/createMessage\"}\n\n",true)==-ENOTSUP);
  assert(perform("data: bad json\n\n",true)==-EPROTO);
  assert(perform("data: {\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{}}",true)==-EPROTO);
  redirect=true;assert(perform("",false)==-EACCES);redirect=false;
  header_truncated=true;assert(perform("",false)==-EFBIG);header_truncated=false;
  overlong=true;assert(perform("",false)==-EFBIG);overlong=false;
  wire_session="bad session";assert(perform("",false)==-EPROTO);wire_session=NULL;
  atomic_bool cancel=true;unsigned before=requests;
  struct glass_mcp_http request={.cancel=&cancel};assert(glass_mcp_http_perform(&request)==-ECANCELED&&requests==before);
  puts("MCP_HTTP_OK: real JSON/SSE parser; one-byte fragments; multiline data/notifications; IDs; headers; limits; redirects; cancellation; unsupported server requests");return 0;
}
