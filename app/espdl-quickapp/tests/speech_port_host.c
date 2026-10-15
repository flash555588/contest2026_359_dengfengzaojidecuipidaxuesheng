/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include "glass_speech_port.c"
static void *try_other_thread(void *p){assert(!xQueueSemaphoreTake(p,0));return NULL;}
int main(void)
{
  void *m=xQueueCreateMutex(1);assert(m&&xQueueSemaphoreTake(m,UINT32_MAX));
  pthread_t thread;assert(!pthread_create(&thread,NULL,try_other_thread,m));assert(!pthread_join(thread,NULL));
  assert(xQueueGenericSend(m,NULL,0,0));assert(xQueueSemaphoreTake(m,0));
  assert(xQueueGenericSend(m,NULL,0,0));vQueueDelete(m);assert(!xQueueCreateMutex(2));
  assert(!heap_caps_calloc(SIZE_MAX,2,0));
  void *p=heap_caps_aligned_alloc(64,1024,0);assert(p&&!((uintptr_t)p&63));heap_caps_free(p);
  const float edge[]={-0.99999994f,-.9f,-.5f,-1e-8f,-FLT_MIN,-0.0f,0,FLT_MIN,1e-8f,.5f,1,20,FLT_MAX};
  for(unsigned i=0;i<sizeof(edge)/sizeof(*edge);i++){
    float ref=log1pf(edge[i]),got=speech_log1pf(edge[i]);
    assert(fabsf(ref-got)<=4*FLT_EPSILON*fabsf(ref)+FLT_MIN);
  }
  for(int i=0;i<10000;i++){float x=(float)i/1000-.999f;float ref=log1pf(x);assert(fabsf(ref-speech_log1pf(x))<4*FLT_EPSILON*fabsf(ref)+FLT_MIN);}
  assert(signbit(speech_log1pf(-0.0f)));assert(isinf(speech_log1pf(INFINITY)));assert(isnan(speech_log1pf(-2)));
  float a[]={1,2,3},b[]={4,5,6},sum;assert(!dsps_dotprod_f32_arp4(a,b,&sum,3)&&sum==32);
  puts("SPEECH_PORT_HOST_OK: private mutex contention, aligned allocations, dot product, 10013 log1p comparisons");
}
