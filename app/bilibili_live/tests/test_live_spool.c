#include "bili_spool.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#define TOTAL (7u*1024u*1024u+321u)
struct source {size_t pos;bool error;};
static int input(void *ctx,uint8_t *p,size_t n){struct source *s=ctx;if(s->pos>=TOTAL)return s->error?-EIO:0;if(n>TOTAL-s->pos)n=TOTAL-s->pos;for(size_t i=0;i<n;i++)p[i]=(s->pos+i)*13u+7u;s->pos+=n;return n;}
static void test(const char *path,bool error){atomic_bool cancel=false;struct source in={.error=error};struct bili_spool *s=bili_spool_open(path,input,&in,&cancel);assert(s);uint8_t p[7777];size_t count=0;int r;
  usleep(50000);while((r=bili_spool_read(s,p,sizeof(p)))>0){for(int i=0;i<r;i++)assert(p[i]==(uint8_t)((count+i)*13u+7u));count+=r;}assert(count==TOTAL&&r==(error?-EIO:0));bili_spool_close(s);}
int main(){char dir[]="/tmp/p4-spool-XXXXXX";assert(mkdtemp(dir));test(dir,false);test(dir,true);
 atomic_bool cancel=false;struct source in={0};struct bili_spool *s=bili_spool_open(dir,input,&in,&cancel);assert(s);usleep(50000);atomic_store(&cancel,true);uint8_t p;assert(bili_spool_read(s,&p,1)==-ECANCELED);bili_spool_close(s);
 assert(!bili_spool_open("/absent-p4-spool",input,&in,&cancel));assert(!rmdir(dir));puts("PASS SD ring wrap, bounded producer, byte ordering, EOF/error, cancellation and owned-file cleanup");}
