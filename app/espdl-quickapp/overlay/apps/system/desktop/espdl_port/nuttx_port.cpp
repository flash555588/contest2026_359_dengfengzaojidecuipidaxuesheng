/* SPDX-License-Identifier: Apache-2.0 */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" long lrintf(float value)
{
  long result;
  __asm__ volatile("fcvt.w.s %0, %1, dyn" : "=r"(result) : "f"(value));
  return result;
}

extern "C" void *qpk_dl_malloc(size_t size, uint32_t caps)
{
  /* P4 vector loads require 16-byte alignment, including resize index maps. */
  return (caps & MALLOC_CAP_SIMD) ? memalign(16, size) : malloc(size);
}
extern "C" void *qpk_dl_calloc(size_t n, size_t size, uint32_t caps)
{
  if (size && n > SIZE_MAX / size) return nullptr;
  void *p=qpk_dl_malloc(n * size,caps);
  if (p) memset(p,0,n * size);
  return p;
}
extern "C" void *qpk_dl_aligned_alloc(size_t align, size_t size, uint32_t caps)
{
  if ((caps & MALLOC_CAP_SIMD) && align < 16) align=16;
  return memalign(align, size);
}
extern "C" void *qpk_dl_aligned_calloc(size_t align, size_t n, size_t size, uint32_t caps)
{
  if (size && n > SIZE_MAX / size) return nullptr;
  void *p = qpk_dl_aligned_alloc(align, n * size, caps);
  if (p) memset(p, 0, n * size);
  return p;
}
extern "C" void qpk_dl_free(void *p) { free(p); }
extern "C" size_t qpk_dl_free_size(uint32_t caps)
{ return (caps & MALLOC_CAP_INTERNAL) ? 0 : mallinfo().fordblks; }
extern "C" size_t qpk_dl_largest_block(uint32_t caps)
{ return (caps & MALLOC_CAP_INTERNAL) ? 0 : mallinfo().mxordblk; }
extern "C" int64_t qpk_dl_timer_us(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
extern "C" void qpk_dl_log(int level, const char *tag, const char *format, ...)
{
  if (level > 2) return;
  printf("[espdl:%s] ", tag);
  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  va_end(ap);
}
