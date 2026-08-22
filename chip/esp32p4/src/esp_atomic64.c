/****************************************************************************
 * arch/risc-v/src/esp32p4/esp_atomic64.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * RV32 has no native 64-bit atomics. HAL gpio reserve uses C11 atomics on
 * uint64_t, which GCC lowers to libatomic helpers this toolchain does not
 * ship. Provide IRQ-safe equivalents for the kernel.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>

#include <stdint.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint64_t __atomic_fetch_or_8(volatile void *ptr, uint64_t val, int memorder)
{
  irqstate_t flags;
  FAR volatile uint64_t *p = (FAR volatile uint64_t *)ptr;
  uint64_t old;

  (void)memorder;

  flags = up_irq_save();
  old = *p;
  *p = old | val;
  up_irq_restore(flags);
  return old;
}

uint64_t __atomic_fetch_and_8(volatile void *ptr, uint64_t val, int memorder)
{
  irqstate_t flags;
  FAR volatile uint64_t *p = (FAR volatile uint64_t *)ptr;
  uint64_t old;

  (void)memorder;

  flags = up_irq_save();
  old = *p;
  *p = old & val;
  up_irq_restore(flags);
  return old;
}

uint64_t __atomic_load_8(FAR const volatile void *ptr, int memorder)
{
  irqstate_t flags;
  uint64_t val;

  (void)memorder;

  flags = up_irq_save();
  val = *(FAR const volatile uint64_t *)ptr;
  up_irq_restore(flags);
  return val;
}

void __atomic_store_8(volatile void *ptr, uint64_t val, int memorder)
{
  irqstate_t flags;

  (void)memorder;

  flags = up_irq_save();
  *(FAR volatile uint64_t *)ptr = val;
  up_irq_restore(flags);
}
