/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <malloc.h>
/* Shared execution guard; renderers yield cooperatively before this limit.
 * Embedded companions must not add shorter application-specific cutoffs. */
#define QPK_JS_EVENT_TIMEOUT_MS 5000
#define QPK_JS_COMPILE_TIMEOUT_MS 10000

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
