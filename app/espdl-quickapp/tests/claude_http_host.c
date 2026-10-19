#include "qpk_cli_origin.h"
/* SPDX-License-Identifier: Apache-2.0 */
#include "../overlay/apps/system/desktop/glass_claude.c"
#include <assert.h>
#include <unistd.h>
static unsigned calls;
static int mode;
static const char token[]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
void webclient_set_defaults(struct webclient_context *c){memset(c,0,sizeof(*c));}
void webclient_set_static_body(struct webclient_context *c,const char *body,size_t length)
{(void)c;assert(length==strlen(body));cJSON *parsed=cJSON_Parse(body);assert(cJSON_IsObject(parsed));cJSON_Delete(parsed);}
int webclient_perform(struct webclient_context *c)
{
  calls++;assert(!strcmp(c->url,"http://192.168.31.95:8787/api/status"));assert(c->timeout_sec==5);
  assert(c->nheaders==4&&strstr(c->headers[3],token));
  if(mode==1)return -ETIMEDOUT;
  if(mode==2)return c->header_callback("Location: http://example.com",false,NULL);
  if(mode==3){char block[2048];memset(block,'x',sizeof(block));char *p=block;int length=sizeof(block);
    for(unsigned i=0;i<9;i++){int ret=c->sink_callback(&p,0,length,&length,c->sink_callback_arg);if(ret)return ret;}assert(false);}
  c->http_status=mode==4?403:200;
  char *body=mode==4?"{\"ok\":false,\"error\":\"denied\"}":"{\"ok\":true,\"projects\":[],\"tasks\":[],\"task\":null}";
  int length=strlen(body);
  for(int i=0;i<length;i++)assert(!c->sink_callback(&body,i,i+1,&length,c->sink_callback_arg));
  return 0;
}
static cJSON *complete(int id)
{
  for(unsigned i=0;i<1000;i++){
    char *text=glass_claude_poll(id);assert(text);cJSON *parsed=cJSON_Parse(text);free(text);assert(parsed);
    if(cJSON_IsTrue(cJSON_GetObjectItem(parsed,"done")))return parsed;
    cJSON_Delete(parsed);usleep(1000);
  }
  assert(false);return NULL;
}
int main(void)
{
  assert(qpk_builtin_cli_origin("ai.claude_control","builtin:/espclaw/cli.js"));
  assert(!qpk_builtin_cli_origin("ai.claude_control","/data/qpk/ai.claude_control/app.js"));
  assert(!qpk_builtin_cli_origin("preview.ai.claude_control","builtin:/espclaw/cli.js"));
  assert(!qpk_builtin_cli_origin(NULL,NULL));

  const char *bad[]={"http://127.0.0.1:8787","http://8.8.8.8:8787","http://192.168.1.2:8787/", "http://192.168.1.2:0","http://192.168.1.2:70000","https://192.168.1.2:8787","http://192.168.1.2:8787@evil.test","http://0192.168.1.2:8787"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(glass_claude_request(bad[i],token,"status","{}")==-EINVAL);
  assert(glass_claude_request("http://10.0.0.2:8787","bad","status","{}")==-EINVAL);
  assert(glass_claude_request("http://10.0.0.2:8787",token,"shell","{}")==-EINVAL);
  assert(!calls);
  for(unsigned i=0;i<100;i++){
    mode=i%5;int id=glass_claude_request("http://192.168.31.95:8787",token,"status","{}");assert(id>0);
    assert(glass_claude_request("http://192.168.31.95:8787",token,"status","{}")==-EBUSY);
    cJSON *result=complete(id);int error=cJSON_GetObjectItem(result,"error")->valueint;
    assert(error==(mode==1?-ETIMEDOUT:mode==2?-EACCES:mode==3?-EFBIG:0));
    if(mode==4)assert(cJSON_GetObjectItem(result,"status")->valueint==403);
    cJSON_Delete(result);result=complete(id);assert(cJSON_GetObjectItem(result,"error")->valueint==-ENOENT);cJSON_Delete(result);
  }
  assert(calls==100&&!claude_job);
  mode=0;
  for(unsigned i=0;i<20;i++){
    int id=glass_claude_request("http://192.168.31.95:8787",token,"status","{}");assert(id>0);
    glass_claude_abandon();cJSON *result=complete(id);
    assert(cJSON_GetObjectItem(result,"error")->valueint==-ENOENT);cJSON_Delete(result);
  }
  assert(calls==120&&!claude_job);
  puts("CLAUDE_HTTP_OK: private IPv4 only; bounded/depth-checked JSON; one flight; 100 success/error cycles; no redirect/replay; result consumed once");
}
