/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
static pthread_mutex_t gate_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_wake=PTHREAD_COND_INITIALIZER;
static bool held,entered;
static pthread_t producer;
static atomic_bool fail_alloc;
static void *test_alloc(size_t size){return atomic_exchange(&fail_alloc,false)?NULL:malloc(size);}
static int test_sync(int fd)
{
  assert(!pthread_equal(pthread_self(),producer));
  pthread_mutex_lock(&gate_lock);entered=true;pthread_cond_broadcast(&gate_wake);
  while(held)pthread_cond_wait(&gate_wake,&gate_lock);
  pthread_mutex_unlock(&gate_lock);return fsync(fd);
}
#define malloc test_alloc
#define fsync test_sync
#include "../overlay/apps/system/desktop/qpk_photo_store.c"
#undef malloc
#undef fsync

static void hold(void){pthread_mutex_lock(&gate_lock);held=true;entered=false;pthread_mutex_unlock(&gate_lock);}
static void wait_gate(void){pthread_mutex_lock(&gate_lock);while(!entered)pthread_cond_wait(&gate_wake,&gate_lock);pthread_mutex_unlock(&gate_lock);}
static void release_gate(void){pthread_mutex_lock(&gate_lock);held=false;pthread_cond_broadcast(&gate_wake);pthread_mutex_unlock(&gate_lock);}
static struct qpk_photo_result wait_result(enum qpk_photo_owner owner,uint32_t ticket)
{
  struct qpk_photo_result result;
  for(unsigned i=0;i<5000;i++){
    assert(qpk_photo_store_status(owner,ticket,&result)==0);
    if(result.done)return result;usleep(1000);
  }abort();
}
int main(void)
{
  producer=pthread_self();assert(qpk_photo_store_initialize()==0);
  uint16_t *pixels=malloc(320*240*2);assert(pixels);
  for(unsigned i=0;i<320*240;i++)pixels[i]=0xf800;
  struct qpk_photo_input rgb={.format=QPK_PHOTO_BMP24,.data=pixels,.bytes=320*240*2,
    .width=320,.height=240,.stride=320,.zoom=100,.root_override=TEST_ROOT "/photos"};
  uint32_t ticket=0,other=0,ignored;
  hold();uint64_t started=now_us();
  assert(qpk_photo_store_submit(QPK_PHOTO_ASSISTANT,&rgb,&ticket)==0);
  assert(now_us()-started<100000); /* Deliberately held filesystem never blocks submit. */
  memset(pixels,0,320*240*2);wait_gate();
  struct qpk_photo_result result;
  assert(qpk_photo_store_status(QPK_PHOTO_ASSISTANT,ticket,&result)==0&&!result.done);
  assert(qpk_photo_store_submit(QPK_PHOTO_ASSISTANT,&rgb,&ignored)==-EBUSY);
  const unsigned char jpeg[]={0xff,0xd8,0xff,0xd9};
  struct qpk_photo_input jpg={.format=QPK_PHOTO_JPEG,.data=jpeg,.bytes=sizeof(jpeg),.root_override=TEST_ROOT "/photos"};
  assert(qpk_photo_store_submit(QPK_PHOTO_PREVIEW,&jpg,&other)==0);
  qpk_photo_store_cancel(QPK_PHOTO_PREVIEW,other);release_gate();
  result=wait_result(QPK_PHOTO_ASSISTANT,ticket);assert(!result.error&&result.path[0]);
  FILE *file=fopen(result.path,"rb");assert(file);uint8_t header[57];assert(fread(header,1,sizeof(header),file)==sizeof(header));fclose(file);
  assert(header[0]=='B'&&header[28]==24&&header[54]==0&&header[55]==0&&header[56]==255);
  result=wait_result(QPK_PHOTO_PREVIEW,other);assert(result.error==-ECANCELED&&!result.path[0]);
  hold();assert(qpk_photo_store_submit(QPK_PHOTO_PREVIEW,&jpg,&other)==0);wait_gate();
  qpk_photo_store_cancel(QPK_PHOTO_PREVIEW,other);
  assert(qpk_photo_store_status(QPK_PHOTO_PREVIEW,other,&result)==0&&!result.done);
  release_gate();result=wait_result(QPK_PHOTO_PREVIEW,other);assert(result.error==-ECANCELED&&!result.path[0]);
  uint32_t stale=other;
  assert(qpk_photo_store_submit(QPK_PHOTO_PREVIEW,&jpg,&other)==0);
  assert(qpk_photo_store_status(QPK_PHOTO_PREVIEW,stale,&result)==-ESTALE);
  result=wait_result(QPK_PHOTO_PREVIEW,other);assert(!result.error);
  file=fopen(result.path,"rb");assert(file);unsigned char saved[4];assert(fread(saved,1,4,file)==4&&!memcmp(saved,jpeg,4));fclose(file);
  rgb.format=QPK_PHOTO_BMP565;rgb.style=2;
  assert(qpk_photo_store_submit(QPK_PHOTO_PREVIEW,&rgb,&other)==0);
  result=wait_result(QPK_PHOTO_PREVIEW,other);assert(!result.error);
  file=fopen(result.path,"rb");assert(file);assert(fread(header,1,54,file)==54&&header[28]==16);fclose(file);
  jpg.bytes=QPK_PHOTO_COPY_MAX+1;
  assert(qpk_photo_store_submit(QPK_PHOTO_PREVIEW,&jpg,&other)==-EINVAL);
  jpg.bytes=4;atomic_store(&fail_alloc,true);
  assert(qpk_photo_store_submit(QPK_PHOTO_PREVIEW,&jpg,&other)==0);
  result=wait_result(QPK_PHOTO_PREVIEW,other);assert(result.error==-ENOMEM);
  free(pixels);qpk_photo_store_shutdown();
  assert(qpk_photo_store_submit(QPK_PHOTO_PREVIEW,&jpg,&other)==-EAGAIN);
  puts("PHOTO_ASYNC_OK: slow fsync off producer, independent copied payload, two bounded owners, duplicate rejection, stale ID, queued/fsync cancellation, JPEG/BMP565/BMP24, allocation error and clean shutdown");
  return 0;
}
