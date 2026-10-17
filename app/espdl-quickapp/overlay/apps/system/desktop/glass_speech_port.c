/* SPDX-License-Identifier: Apache-2.0
 * The pinned SR kernels use FreeRTOS mutex entry points only. No scheduler
 * ABI or task structs are emulated. Map each checked mutex to a POSIX mutex.
 */
#include "speech_port/esp_heap_caps.h"
#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* This vendor copy primitive uses an unaligned PIE load that traps on the
 * current board. Model copies do not need SIMD; retain the scalar libc path. */
void dl_esp32p4_sr_memcpy(void *destination,const void *source,size_t bytes)
{memcpy(destination,source,bytes);}

void *heap_caps_malloc(size_t n,uint32_t caps)
{(void)caps;return memalign(16,n);}
void *heap_caps_calloc(size_t n,size_t size,uint32_t caps)
{
  if(size&&n>SIZE_MAX/size)return NULL;
  void *p=heap_caps_malloc(n*size,caps);if(p)__builtin_memset(p,0,n*size);return p;
}
void *heap_caps_aligned_alloc(size_t alignment,size_t n,uint32_t caps)
{(void)caps;if(alignment<sizeof(void *)||alignment&(alignment-1))return NULL;return memalign(alignment,n);}
void heap_caps_free(void *p){free(p);}
struct speech_mutex {pthread_mutex_t lock;};
void *xQueueCreateMutex(unsigned char type)
{
  if(type!=1)return NULL;
  struct speech_mutex *m=malloc(sizeof(*m));if(!m)return NULL;
  if(pthread_mutex_init(&m->lock,NULL)){free(m);return NULL;}return m;
}
int xQueueSemaphoreTake(void *handle,uint32_t ticks)
{
  if(!handle)return 0;struct speech_mutex *m=handle;
  if(!ticks)return pthread_mutex_trylock(&m->lock)==0;
  /* Pinned inference uses portMAX_DELAY, never timed waits. */
  if(ticks!=UINT32_MAX)return 0;
  return pthread_mutex_lock(&m->lock)==0;
}
int xQueueGenericSend(void *handle,const void *item,uint32_t ticks,int position)
{
  (void)ticks;(void)position;
  if(!handle||item)return 0;return pthread_mutex_unlock(&((struct speech_mutex *)handle)->lock)==0;
}
void vQueueDelete(void *handle)
{if(handle){pthread_mutex_destroy(&((struct speech_mutex *)handle)->lock);free(handle);}}
/* ESP-DSP's portable dot product has the same float accumulation contract. */
int dsps_dotprod_f32_arp4(const float *a,const float *b,float *out,int n)
{
  if(!a||!b||!out||n<0)return 0x102;
  float sum=0;for(int i=0;i<n;i++)sum+=a[i]*b[i];*out=sum;return 0;
}

/* NuttX's selected libm lacks log1pf. Compensate rounding in 1+x so
 * sub-ulp inputs retain precision; keep this symbol private to speech. */
float speech_log1pf(float x)
{
  float u=1.0f+x;
  if(u==1.0f)return x;
  if(!isfinite(x)||u<=0.0f)return logf(u);
  return logf(u)*(x/(u-1.0f));
}
