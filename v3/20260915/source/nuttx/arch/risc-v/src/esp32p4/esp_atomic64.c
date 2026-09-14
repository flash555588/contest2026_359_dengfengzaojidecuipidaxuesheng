/****************************************************************************
 * arch/risc-v/src/esp32p4/esp_atomic64.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * RV32 GCC emits these helpers for QuickJS and the HAL. A single IRQ-safe
 * spinlock serializes 64-bit accesses across both cores, including boot.
 * The lock provides stronger ordering than relaxed operations require.
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/spinlock.h>
#include <stdint.h>
#include <stdbool.h>

static spinlock_t g_atomic64_lock = SP_UNLOCKED;

#define ATOMIC64_FETCH(name, expression) \
  uint64_t name(volatile void *ptr, uint64_t value, int order) \
  { \
    volatile uint64_t *p = ptr; \
    irqstate_t flags = spin_lock_irqsave_notrace(&g_atomic64_lock); \
    uint64_t old = *p; \
    (void)order; \
    *p = (expression); \
    spin_unlock_irqrestore_notrace(&g_atomic64_lock, flags); \
    return old; \
  }

ATOMIC64_FETCH(__atomic_fetch_add_8, old + value)
ATOMIC64_FETCH(__atomic_fetch_sub_8, old - value)
ATOMIC64_FETCH(__atomic_fetch_and_8, old & value)
ATOMIC64_FETCH(__atomic_fetch_or_8, old | value)
ATOMIC64_FETCH(__atomic_fetch_xor_8, old ^ value)
ATOMIC64_FETCH(__atomic_exchange_8, value)

uint64_t __atomic_load_8(const volatile void *ptr, int order)
{
  irqstate_t flags = spin_lock_irqsave_notrace(&g_atomic64_lock);
  uint64_t value = *(const volatile uint64_t *)ptr;
  (void)order;
  spin_unlock_irqrestore_notrace(&g_atomic64_lock, flags);
  return value;
}

void __atomic_store_8(volatile void *ptr, uint64_t value, int order)
{
  irqstate_t flags = spin_lock_irqsave_notrace(&g_atomic64_lock);
  (void)order;
  *(volatile uint64_t *)ptr = value;
  spin_unlock_irqrestore_notrace(&g_atomic64_lock, flags);
}

bool __atomic_compare_exchange_8(volatile void *ptr, void *expected,
                                 uint64_t desired, bool weak,
                                 int success_order, int failure_order)
{
  volatile uint64_t *p = ptr;
  uint64_t *expected_value = expected;
  irqstate_t flags = spin_lock_irqsave_notrace(&g_atomic64_lock);
  bool equal = *p == *expected_value;
  (void)weak;
  (void)success_order;
  (void)failure_order;

  if (equal)
    {
      *p = desired;
    }
  else
    {
      *expected_value = *p;
    }

  spin_unlock_irqrestore_notrace(&g_atomic64_lock, flags);
  return equal;
}
