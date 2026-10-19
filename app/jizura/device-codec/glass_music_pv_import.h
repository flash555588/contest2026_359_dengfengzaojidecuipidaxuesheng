/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_MUSIC_PV_IMPORT_H
#define GLASS_MUSIC_PV_IMPORT_H
#include <netutils/webclient.h>
#include <fcntl.h>
#include <unistd.h>
struct jpv_import {int fd;size_t bytes;};
static int jpv_import_sink(char **buffer,int start,int end,int *length,void *arg)
{
  struct jpv_import *s=arg;
  if(start<0||end<start||end>*length||s->bytes+(size_t)(end-start)>32u*1024*1024)return -EFBIG;
  while(start<end){ssize_t n=write(s->fd,*buffer+start,end-start);if(n<0&&errno==EINTR)continue;if(n<=0)return -EIO;start+=n;s->bytes+=n;}
  return 0;
}
/* Physical-console fixture import only: numbered assets from a private LAN,
 * into a fresh file in an explicit SD staging directory. No listener or
 * credentials, no arbitrary destination, and no replacement of user files. */
static int jpv_import_file(const char *url,const char *name)
{
  unsigned a,b,c,d,port,index;int used=0;
  if(sscanf(url,"http://%u.%u.%u.%u:%u/%u%n",&a,&b,&c,&d,&port,&index,&used)!=6||url[used]||a>255||b>255||c>255||d>255||port!=8774||index>=30||
     !(a==10||(a==192&&b==168)||(a==172&&b>=16&&b<=31)))return -EINVAL;
  unsigned item;used=0;if(sscanf(name,"f%u.part%n",&item,&used)!=1||name[used]||item>=30)return -EINVAL;
  char cwd[128];if(!getcwd(cwd,sizeof(cwd))||strncmp(cwd,"/sdcard/.jpv-",13)||strlen(cwd)!=21)return -EINVAL;
  for(unsigned n=13;n<21;n++)if(!strchr("0123456789abcdef",cwd[n]))return -EINVAL;
  struct jpv_import s={.fd=open(name,O_WRONLY|O_CREAT|O_EXCL,0600)};if(s.fd<0)return -errno;
  char *buffer=malloc(32768);int ret=-ENOMEM;
  if(buffer){
    struct webclient_context client;webclient_set_defaults(&client);client.url=url;client.buffer=buffer;client.buflen=32768;
    client.sink_callback=jpv_import_sink;client.sink_callback_arg=&s;client.timeout_sec=15;
    ret=webclient_perform(&client);if(!ret&&client.http_status!=200)ret=-EIO;free(buffer);
  }
  if(close(s.fd)<0&&!ret)ret=-errno;
  printf("JIZURA_IMPORT bytes=%lu error=%d\n",(unsigned long)s.bytes,ret);return ret;
}
#endif
