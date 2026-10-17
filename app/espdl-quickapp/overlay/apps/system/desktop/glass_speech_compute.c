/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_speech_compute.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#if defined(__riscv)
#include <nuttx/config.h>
#include <sched.h>
#include <stdio.h>
#endif

struct compute_scope {
  intptr_t escape[5];
  speech_compute_cancel_fn cancel;
  void *user;
  void *argument;
  struct compute_scope *previous;
};
static pthread_once_t compute_once=PTHREAD_ONCE_INIT;
static pthread_key_t compute_key;
static int compute_key_error;
static void compute_key_init(void)
{compute_key_error=pthread_key_create(&compute_key,NULL);}

int glass_speech_compute_run(speech_compute_body_fn body,void *argument,
                             speech_compute_cancel_fn cancel,void *user)
{
  if(!body)return -EINVAL;
  int ret=pthread_once(&compute_once,compute_key_init);if(ret)return -ret;
  if(compute_key_error)return -compute_key_error;
  struct compute_scope scope;
  scope.cancel=cancel;scope.user=user;scope.argument=argument;scope.previous=pthread_getspecific(compute_key);
  ret=pthread_setspecific(compute_key,&scope);if(ret)return -ret;
  /* This NuttX configuration does not link libc setjmp. GCC's paired
   * nonlocal-goto builtins use a five-intptr frame buffer. Only same-ISA
   * compute is guarded. Never jump out of PIE/assembly SR inference; custom
   * allocation-free PIE leaves must return to C before a checkpoint.
   * There are no malloc, locks, handles or C++ destructors in this scope. */
  if(__builtin_setjmp(scope.escape)==0){
    glass_speech_compute_checkpoint();
    ret=body(argument);
  }else ret=-ECANCELED;
  int restored=pthread_setspecific(compute_key,scope.previous);
  return restored?-restored:ret;
}
void glass_speech_compute_checkpoint(void)
{
  if(pthread_once(&compute_once,compute_key_init)||compute_key_error)return;
  struct compute_scope *scope=pthread_getspecific(compute_key);
  if(scope&&scope->cancel&&scope->cancel(scope->user))__builtin_longjmp(scope->escape,1);
}
void *glass_speech_compute_argument(void)
{
  if(pthread_once(&compute_once,compute_key_init)||compute_key_error)return NULL;
  struct compute_scope *scope=pthread_getspecific(compute_key);return scope?scope->argument:NULL;
}

struct parallel_call {
  speech_compute_body_fn body;void *argument;
  struct compute_scope *parent;
  pthread_mutex_t cancel_lock;
  atomic_bool cancelled;
  int result;
  int cpu;
  int affinity_result;
};
static bool parallel_cancel(void *opaque)
{
  struct parallel_call *p=opaque;
  pthread_mutex_lock(&p->cancel_lock);
  if(!atomic_load(&p->cancelled)&&p->parent&&p->parent->cancel&&p->parent->cancel(p->parent->user))
    atomic_store(&p->cancelled,true);
  bool cancelled=atomic_load(&p->cancelled);
  pthread_mutex_unlock(&p->cancel_lock);return cancelled;
}
static void *parallel_worker(void *opaque)
{
  struct parallel_call *p=opaque;
#if defined(__riscv)
  /* A new pthread starts with PIE disabled. Its context is isolated by the
   * same extended-register hooks as the main speech worker. */
  __asm__ volatile("csrwi 0x7f2, 1\ncsrwi 0x7f1, 1" ::: "memory");
#endif
#if defined(__riscv) && defined(CONFIG_SMP)
  cpu_set_t set;CPU_ZERO(&set);CPU_SET(1,&set);
  p->affinity_result=pthread_setaffinity_np(pthread_self(),sizeof(set),&set);
  sched_yield();p->cpu=sched_getcpu();
#endif
  p->result=glass_speech_compute_run(p->body,p->argument,parallel_cancel,p);
  return NULL;
}
void glass_speech_compute_parallel(speech_compute_body_fn body,void *left,void *right)
{
  if(pthread_once(&compute_once,compute_key_init)||compute_key_error){body(left);body(right);return;}
  struct compute_scope *parent=pthread_getspecific(compute_key);
  struct parallel_call p={.body=body,.argument=right,.parent=parent,
    .cancel_lock=PTHREAD_MUTEX_INITIALIZER};
  atomic_init(&p.cancelled,false);
  pthread_t worker;pthread_attr_t attr;
#if defined(__riscv) && defined(CONFIG_SMP)
  cpu_set_t saved,main_set,worker_set;
  bool pinned=false;
  if(!pthread_getaffinity_np(pthread_self(),sizeof(saved),&saved)){
    CPU_ZERO(&main_set);CPU_SET(0,&main_set);
    pinned=!pthread_setaffinity_np(pthread_self(),sizeof(main_set),&main_set);
  }
#endif
  int ret=pthread_attr_init(&attr);
  if(!ret){ret=pthread_attr_setstacksize(&attr,32*1024);
#if defined(__riscv) && defined(CONFIG_SMP)
    if(!ret&&pinned){CPU_ZERO(&worker_set);CPU_SET(1,&worker_set);
      ret=pthread_attr_setaffinity_np(&attr,sizeof(worker_set),&worker_set);}
#endif
    if(!ret)ret=pthread_create(&worker,&attr,parallel_worker,&p);
    pthread_attr_destroy(&attr);}
  if(ret){
#if defined(__riscv) && defined(CONFIG_SMP)
    if(pinned)pthread_setaffinity_np(pthread_self(),sizeof(saved),&saved);
#endif
    pthread_mutex_destroy(&p.cancel_lock);body(left);body(right);return;}
  int main_result=glass_speech_compute_run(body,left,parallel_cancel,&p);
  /* No checkpoint in this join: caller arenas and p live until worker exit. */
  int joined;do{joined=pthread_join(worker,NULL);}while(joined==EINTR);
  bool cancelled=main_result==-ECANCELED||p.result==-ECANCELED;
#if defined(__riscv) && defined(CONFIG_SMP)
  static atomic_bool reported;
  if(!atomic_exchange(&reported,true))printf("[speech] parallel main_cpu=%d worker_cpu=%d pinned=%u affinity_result=%d\n",sched_getcpu(),p.cpu,pinned,p.affinity_result);
  if(pinned)pthread_setaffinity_np(pthread_self(),sizeof(saved),&saved);
#endif
  pthread_mutex_destroy(&p.cancel_lock);
  if(cancelled&&parent)__builtin_longjmp(parent->escape,1);
}
