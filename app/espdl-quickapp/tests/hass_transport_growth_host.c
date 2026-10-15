/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef void *pthread_addr_t;
#define OK 0
#include "../overlay/apps/system/hass/hass_transport.c"

static int listener;
static size_t body_size;
static bool chunked,registry_query,scene_query;
static void send_bytes(int fd,const char *text,size_t size)
{while(size){ssize_t n=send(fd,text,size,MSG_NOSIGNAL);if(n<=0)return;text+=n;size-=n;}}
static void *server(void *unused)
{
  (void)unused;int fd=accept(listener,NULL,NULL);assert(fd>=0);
  char input[4096];ssize_t n=recv(fd,input,sizeof(input)-1,0);assert(n>0);input[n]=0;
  assert(strstr(input,registry_query?"POST /api/template ":"GET /api/states ")&&strstr(input,"Bearer test-token"));
  if(registry_query) {
    size_t used=n;
    const char *query=scene_query?g_hass_scenes_template:g_hass_devices_template;
    while(!strstr(input,query)) {
      n=recv(fd,input+used,sizeof(input)-1-used,0);assert(n>0);used+=n;input[used]=0;
    }
    assert(strstr(input,scene_query?"states.scene":"device_entities(d)")&&strstr(input,"Content-Type: application/json"));
  }
  char header[160];snprintf(header,sizeof(header),chunked?
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n":
    "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",body_size);
  send_bytes(fd,header,strlen(header));char body[1024];memset(body,'x',sizeof(body));
  for(size_t i=0;i<body_size;){size_t count=body_size-i<sizeof(body)?body_size-i:sizeof(body);
    if(chunked){snprintf(header,sizeof(header),"%zx\r\n",count);send_bytes(fd,header,strlen(header));}
    send_bytes(fd,body,count);if(chunked)send_bytes(fd,"\r\n",2);i+=count;}
  if(chunked)send_bytes(fd,"0\r\n\r\n",5);close(fd);return NULL;
}
static void run(size_t size,bool use_chunks,bool success)
{
  body_size=size;chunked=use_chunks;
  listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
  struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
  assert(!bind(listener,(struct sockaddr *)&addr,sizeof(addr)));assert(!listen(listener,1));
  socklen_t len=sizeof(addr);assert(!getsockname(listener,(struct sockaddr *)&addr,&len));
  pthread_t thread;assert(!pthread_create(&thread,NULL,server,NULL));
  struct ha_request_s request={.host="127.0.0.1",.path="/api/states",.token="test-token",.port=ntohs(addr.sin_port)};
  if(registry_query){strcpy(request.path,"/api/template");strcpy(request.body,scene_query?g_hass_scenes_template:g_hass_devices_template);request.post=true;}
  char *response=NULL;int status=0;
  int ret=ha_request_run(&request,&status,&response,HA_RESPONSE_MAX);
  if(success){assert(!ret&&status==200&&strlen(response)==size);for(size_t i=0;i<size;i++)assert(response[i]=='x');}
  else assert(ret==-EOVERFLOW&&!response);
  free(response);pthread_join(thread,NULL);close(listener);
}
int main(void)
{
  run(1000,false,true);run(90487,false,true);run(256*1024,false,true);
  run(90487,true,true);run(300*1024,false,false);
  registry_query=true;run(12057,false,true);
  scene_query=true;run(4096,false,true);
  puts("HA real transport growth tests passed: small, 90 KB, 256 KB, chunked, bounded overflow");
}
