/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
static unsigned opens,closes,requests,written;static int mode;static bool outside;
static char *fixture_getcwd(char *out,size_t size);
static int fixture_open(const char *path,int flags,...);
static ssize_t fixture_write(int fd,const void *data,size_t size);
static int fixture_close(int fd);
#define getcwd fixture_getcwd
#define open fixture_open
#define write fixture_write
#define close fixture_close
#include "glass_music_pv_import.h"
#undef getcwd
#undef open
#undef write
#undef close
static char *fixture_getcwd(char *out,size_t size)
{snprintf(out,size,"%s",outside?"/sdcard/music":"/sdcard/.jpv-01234567");return out;}
static int fixture_open(const char *path,int flags,...)
{assert(!strcmp(path,"f0.part")&&(flags&O_EXCL));if(mode==1){errno=EEXIST;return -1;}opens++;return 7;}
static ssize_t fixture_write(int fd,const void *data,size_t size)
{assert(fd==7&&data);size_t n=size>1?size/2:size;written+=n;return n;}
static int fixture_close(int fd){assert(fd==7);closes++;return 0;}
void webclient_set_defaults(struct webclient_context *c){memset(c,0,sizeof(*c));}
int webclient_perform(struct webclient_context *c)
{
  requests++;assert(c->buflen==32768&&c->timeout_sec==15);c->http_status=mode==2?404:200;
  int n=c->buflen;memset(c->buffer,37,n);
  unsigned count=mode==3?1025:2;
  for(unsigned i=0;i<count;i++){int ret=c->sink_callback(&c->buffer,0,n,&n,c->sink_callback_arg);if(ret)return ret;}
  return mode==4?-ETIMEDOUT:0;
}
int main(void)
{
  const char *url="http://192.168.31.95:8774/0";
  assert(!jpv_import_file(url,"f0.part"));assert(opens==closes&&written==65536);
  unsigned before=requests;mode=1;assert(jpv_import_file(url,"f0.part")==-EEXIST&&requests==before);
  mode=2;assert(jpv_import_file(url,"f0.part")==-EIO);mode=3;assert(jpv_import_file(url,"f0.part")==-EFBIG);
  mode=4;assert(jpv_import_file(url,"f0.part")==-ETIMEDOUT);mode=0;
  before=opens;
  assert(jpv_import_file("http://8.8.8.8:8774/0","f0.part")==-EINVAL);
  assert(jpv_import_file("https://192.168.31.95:8774/0","f0.part")==-EINVAL);
  assert(jpv_import_file("http://192.168.31.95:8774/30","f0.part")==-EINVAL);
  assert(jpv_import_file(url,"../music.mp3")==-EINVAL);outside=true;
  assert(jpv_import_file(url,"f0.part")==-EINVAL);assert(opens==before&&opens==closes);
  puts("JPV_IMPORT_OK: 32K chunks, partial writes, O_EXCL preservation, close on HTTP/size/timeout errors, private URL/staging/path bounds");
}
