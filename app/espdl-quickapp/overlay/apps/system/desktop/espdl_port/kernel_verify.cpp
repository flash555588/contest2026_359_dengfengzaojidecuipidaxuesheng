/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
#include "dl_module_gemm.hpp"
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
extern "C" void qpk_dl_context_roundtrip(const void *,void *,unsigned);
extern "C" void qpk_dl_fpu_roundtrip(const void *,void *);

static void *check(void *arg)
{
  int seed=(int)(intptr_t)arg;
  printf("[espdl] context worker %d started\n",seed);
  __asm__ volatile("csrwi 0x7f2, 1\ncsrwi 0x7f1, 1" ::: "memory");
  dl_esp32p4_cfg_round(ROUND_MODE_HALF_EVEN);
  alignas(16) uint8_t state[224]={0},saved[224]={0};
  uint32_t fstate[33],fsaved[33];
  for(int i=0;i<32;i++) fstate[i]=0x3f000000+seed*0x10000+i*0x100;
  fstate[32]=(seed-1)*32; // Different legal rounding modes on the same CPU.
  for(int i=0;i<4;i++) {
    qpk_dl_fpu_roundtrip(fstate,fsaved);
    if(memcmp(fstate,fsaved,sizeof(fstate))) {
      printf("[espdl] FPU mismatch worker=%d trial=%d fcsr=%lu\n",seed,i,(unsigned long)fsaved[32]);
      return(void *)(intptr_t)-EILSEQ;
    }
  }
  __asm__ volatile("fscsr zero" ::: "memory");
  for(int i=0;i<208;i++) state[i]=(i*31+seed*17)&255;
  using namespace dl;
  TensorBase input({1,16},nullptr,-3,DATA_TYPE_INT8);
  TensorBase output({1,32},nullptr,-5,DATA_TYPE_INT8);
  TensorBase weights({1,1,16,32},nullptr,-3,DATA_TYPE_INT8);
  TensorBase bias({32},nullptr,-6,DATA_TYPE_INT32);
  if(!input.data||!output.data||!weights.data||!bias.data)return(void *)(intptr_t)-ENOMEM;
  for(int c=0;c<16;c++) ((int8_t *)input.data)[c]=(c+seed)%9-4;
  for(int n=0;n<32;n++) {
    ((int32_t *)bias.data)[n]=n-16;
    for(int c=0;c<16;c++) ((int8_t *)weights.data)[(n/16)*256+c*16+n%16]=(n*3+c+seed)%7-3;
  }
  module::Gemm gemm(ReLU,nullptr,QUANT_TYPE_SYMM_8BIT);
  for(int trial=0;trial<100;trial++) {
    if(qpk_dl_should_cancel())return(void *)(intptr_t)-ECANCELED;
    qpk_dl_context_roundtrip(state,saved,(trial%4)*4);
    if(memcmp(state,saved,208)) {
      printf("[espdl] context mismatch worker=%d trial=%d\n",seed,trial);
      return(void *)(intptr_t)-EILSEQ;
    }
    // Module::run appends its tensor indices; reset the temporary binding.
    gemm.reset();
    gemm.run({&input,&weights,&bias},{&output},RUNTIME_MODE_SINGLE_CORE);
    for(int n=0;n<32;n++) {
      int sum=n-16;
      for(int c=0;c<16;c++)sum+=((c+seed)%9-4)*((n*3+c+seed)%7-3);
      int rounded=sum/2;
      if(sum%2 && (rounded&1)) rounded+=sum>0?1:-1;
      if(rounded<0)rounded=0;if(rounded>127)rounded=127;
      if(((int8_t *)output.data)[n]!=rounded) {
        printf("[espdl] Gemm mismatch worker=%d trial=%d channel=%d got=%d expected=%d\n",
               seed,trial,n,((int8_t *)output.data)[n],rounded);
        return(void *)(intptr_t)-EBADMSG;
      }
    }
  }
  return nullptr;
}
extern "C" int qpk_dl_kernel_verify(void)
{
  pthread_attr_t attr;pthread_t threads[2];unsigned started=0;
  int ret=pthread_attr_init(&attr);
  printf("[espdl] starting context selftest\n");
  if(ret)return-ret;
  cpu_set_t cpus;CPU_ZERO(&cpus);CPU_SET(0,&cpus);
  ret=pthread_attr_setstacksize(&attr,32768);
  if(!ret)ret=pthread_attr_setaffinity_np(&attr,sizeof(cpus),&cpus);
  for(;!ret&&started<2;started++)ret=pthread_create(&threads[started],&attr,check,(void *)(intptr_t)(started+1));
  /* A failed creation did not initialize the corresponding pthread_t. */
  if(ret&&started)started--;
  pthread_attr_destroy(&attr);
  int result=ret?-ret:0;
  for(unsigned i=0;i<started;i++) {
    void *status=nullptr;int joined=pthread_join(threads[i],&status);
    if(!result)result=joined?-joined:(int)(intptr_t)status;
  }
  printf("[espdl] P4 SIMD + same-CPU context test result=%d\n",result);
  return result;
}
