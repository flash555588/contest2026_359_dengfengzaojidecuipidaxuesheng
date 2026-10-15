/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_spool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#ifdef __NuttX__
#include <sys/mount.h>
#include <sys/statfs.h>
#endif
#define SPOOL_BYTES (4u*1024u*1024u)
#define CHUNK_BYTES 16384u
struct bili_spool {
  pthread_t thread;pthread_mutex_t lock;pthread_cond_t changed;
  int fd,error;bool done,stop;
  size_t head,tail,used;
  bili_read_fn source;void *ctx;atomic_bool *cancel;
  char path[512];uint8_t chunk[CHUNK_BYTES];
};
static void wait_change(struct bili_spool *s){
  struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);
  deadline.tv_nsec+=100000000;if(deadline.tv_nsec>=1000000000){deadline.tv_nsec-=1000000000;deadline.tv_sec++;}
  pthread_cond_timedwait(&s->changed,&s->lock,&deadline);
}
static int disk_io(struct bili_spool *s,void *p,size_t n,size_t offset,bool writing){
  if(lseek(s->fd,offset,SEEK_SET)<0)return -errno;
  size_t used=0;
  while(used<n){ssize_t r=writing?write(s->fd,(uint8_t *)p+used,n-used):read(s->fd,(uint8_t *)p+used,n-used);
    if(r<0){if(errno==EINTR)continue;return -errno;}if(!r)return -EIO;used+=r;}
  return 0;
}
static void *producer(void *opaque){
  struct bili_spool *s=opaque;int result=0;
  for(;;){
    pthread_mutex_lock(&s->lock);
    while(!s->stop&&!atomic_load(s->cancel)&&SPOOL_BYTES-s->used<CHUNK_BYTES)wait_change(s);
    bool stop=s->stop||atomic_load(s->cancel);pthread_mutex_unlock(&s->lock);if(stop)break;
    int n=s->source(s->ctx,s->chunk,CHUNK_BYTES);if(n<=0){result=n;break;}if(n>CHUNK_BYTES){result=-EPROTO;break;}
    pthread_mutex_lock(&s->lock);
    size_t first=SPOOL_BYTES-s->head;if(first>(size_t)n)first=n;
    result=disk_io(s,s->chunk,first,s->head,true);
    if(!result&&(size_t)n>first)result=disk_io(s,s->chunk+first,n-first,0,true);
    if(!result){s->head=(s->head+n)%SPOOL_BYTES;s->used+=n;pthread_cond_broadcast(&s->changed);}
    pthread_mutex_unlock(&s->lock);if(result)break;
  }
  pthread_mutex_lock(&s->lock);s->error=result;s->done=true;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->lock);return NULL;
}
struct bili_spool *bili_spool_open(const char *directory,bili_read_fn source,void *ctx,atomic_bool *cancel){
#ifdef __NuttX__
  struct statfs fs;
  if(statfs(directory,&fs)<0||fs.f_type!=MSDOS_SUPER_MAGIC)
    if(mount("/dev/mmcsd0",directory,"vfat",0,NULL)<0&&errno!=EBUSY)return NULL;
#endif
  struct bili_spool *s=calloc(1,sizeof(*s));if(!s)return NULL;
  s->source=source;s->ctx=ctx;s->cancel=cancel;
  if(snprintf(s->path,sizeof(s->path),"%s/.live-cache-XXXXXX",directory)>=(int)sizeof(s->path)){free(s);return NULL;}
  s->fd=mkstemp(s->path);if(s->fd<0){free(s);return NULL;}
  int r=pthread_mutex_init(&s->lock,NULL);if(r)goto fd_fail;
  r=pthread_cond_init(&s->changed,NULL);if(r)goto lock_fail;
  pthread_attr_t attr;r=pthread_attr_init(&attr);if(r)goto cond_fail;
  r=pthread_attr_setstacksize(&attr,32768);
  if(!r)r=pthread_create(&s->thread,&attr,producer,s);
  pthread_attr_destroy(&attr);if(r)goto cond_fail;
  return s;
cond_fail:pthread_cond_destroy(&s->changed);
lock_fail:pthread_mutex_destroy(&s->lock);
fd_fail:close(s->fd);unlink(s->path);free(s);return NULL;
}
int bili_spool_read(struct bili_spool *s,uint8_t *p,size_t n){
  if(!n)return 0;pthread_mutex_lock(&s->lock);
  while(!s->used&&!s->done&&!s->stop&&!atomic_load(s->cancel))wait_change(s);
  int r;
  if(s->stop||atomic_load(s->cancel))r=-ECANCELED;
  else if(!s->used)r=s->error;
  else{if(n>s->used)n=s->used;if(n>CHUNK_BYTES)n=CHUNK_BYTES;
    size_t first=SPOOL_BYTES-s->tail;if(first>n)first=n;r=disk_io(s,p,first,s->tail,false);
    if(!r&&n>first)r=disk_io(s,p+first,n-first,0,false);
    if(!r){s->tail=(s->tail+n)%SPOOL_BYTES;s->used-=n;r=n;pthread_cond_broadcast(&s->changed);}}
  pthread_mutex_unlock(&s->lock);return r;
}
void bili_spool_close(struct bili_spool *s){
  if(!s)return;pthread_mutex_lock(&s->lock);s->stop=true;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->lock);
  pthread_join(s->thread,NULL);close(s->fd);unlink(s->path);pthread_cond_destroy(&s->changed);pthread_mutex_destroy(&s->lock);free(s);
}
