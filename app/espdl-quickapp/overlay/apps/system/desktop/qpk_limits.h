/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <malloc.h>

static inline size_t qpk_js_memory_budget(void)
{
#ifdef __NuttX__
  /* Share available RAM with LVGL, networking and native widget allocations.
   * Recompute for every launch/compile instead of a fixed per-app 2 MiB cap. */
  struct mallinfo memory = mallinfo();
  return (size_t)memory.fordblks / 2;
#else
  return 16u * 1024u * 1024u;
#endif
}
