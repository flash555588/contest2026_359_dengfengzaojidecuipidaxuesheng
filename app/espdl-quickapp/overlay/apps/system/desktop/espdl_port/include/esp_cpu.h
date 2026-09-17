/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
static inline uint32_t esp_cpu_get_cycle_count(void)
{
  uint32_t value;
  __asm__ volatile("rdcycle %0" : "=r" (value));
  return value;
}
