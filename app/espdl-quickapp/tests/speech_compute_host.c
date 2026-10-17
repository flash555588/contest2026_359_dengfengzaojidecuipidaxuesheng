/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_speech_compute.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sched.h>

struct work {unsigned count,limit;bool cancel;};
static bool cancel_now(void *opaque)
{struct work *w=opaque;return w->cancel&&w->count>=w->limit;}
static int run_work(void *opaque)
{
  struct work *w=opaque;
  assert(glass_speech_compute_argument()==opaque);
  for(unsigned i=0;i<10000;i++){
    glass_speech_compute_checkpoint();w->count++;
  }
  return 17;
}
static int nested(void *opaque)
{
  struct work inner={.limit=11,.cancel=true};
  assert(glass_speech_compute_run(run_work,&inner,cancel_now,&inner)==-ECANCELED);
  assert(inner.count==11);
  return run_work(opaque);
}
static void *thread_work(void *opaque)
{
  struct work *w=opaque;
  int result=glass_speech_compute_run(nested,w,cancel_now,w);
  assert(result==(w->cancel?-ECANCELED:17));
  assert(w->count==(w->cancel?w->limit:10000));
  struct work clean={0};
  assert(glass_speech_compute_run(run_work,&clean,NULL,NULL)==17&&clean.count==10000);
  glass_speech_compute_checkpoint(); /* No stale scope after either exit. */
  assert(glass_speech_compute_argument()==NULL);
  return NULL;
}
struct parallel_test {atomic_uint entered,progress;};
static int parallel_body(void *opaque)
{
  struct parallel_test *p=opaque;atomic_fetch_add(&p->entered,1);
  while(atomic_load(&p->entered)<2)sched_yield();
  for(unsigned i=0;i<10000;i++){
    glass_speech_compute_checkpoint();atomic_fetch_add(&p->progress,1);
  }
  return 0;
}
static bool parallel_stop(void *opaque)
{return atomic_load(&((struct parallel_test *)opaque)->progress)>=100;}
static int parallel_outer(void *opaque)
{glass_speech_compute_parallel(parallel_body,opaque,opaque);return 0;}
int main(void)
{
  glass_speech_compute_checkpoint();
  assert(glass_speech_compute_run(NULL,NULL,NULL,NULL)==-EINVAL);
  pthread_t threads[8];struct work work[8];
  for(unsigned i=0;i<8;i++){
    work[i]=(struct work){.limit=23+i,.cancel=(i%2)==0};
    assert(!pthread_create(&threads[i],NULL,thread_work,&work[i]));
  }
  for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
  struct parallel_test parallel;atomic_init(&parallel.entered,0);atomic_init(&parallel.progress,0);
  assert(glass_speech_compute_run(parallel_outer,&parallel,parallel_stop,&parallel)==-ECANCELED);
  assert(atomic_load(&parallel.entered)==2&&atomic_load(&parallel.progress)>=100);
  atomic_store(&parallel.entered,0);atomic_store(&parallel.progress,0);
  assert(!glass_speech_compute_run(parallel_outer,&parallel,NULL,NULL));
  assert(atomic_load(&parallel.entered)==2&&atomic_load(&parallel.progress)==20000);
  puts("SPEECH_PARALLEL_HOST_OK: both tasks entered, cancellation propagated after join, fresh parallel scope completed");
  puts("SPEECH_COMPUTE_HOST_OK: nested cancellation, cleanup, fresh scopes and eight-thread isolation; ASan/UBSan");
}
