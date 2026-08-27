/****************************************************************************
 * arch/risc-v/src/common/espressif/esp_allocateheap.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <sys/types.h>

#include <arch/board/board.h>
#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/mm/mm.h>

#include "riscv_internal.h"
#include "rom/rom_layout.h"
#ifdef CONFIG_ARCH_CHIP_ESP32P4
#  include "esp_rom_sys.h"
extern int ets_printf(const char *fmt, ...) printf_like(1, 2);
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_allocate_heap
 *
 * Description:
 *   This function will be called to dynamically set aside the heap region.
 *
 *   For the kernel build (CONFIG_BUILD_KERNEL=y) with both kernel and
 *   userspace heaps (CONFIG_MM_KERNEL_HEAP=y), this function provides the
 *   size of the unprotected, userspace heap.
 *
 *   If a protected kernel heap is provided, the kernel heap must be
 *   allocated (and protected) by an analogous up_allocate_kheap().
 *
 * Input Parameters:
 *   None.
 *
 * Output Parameters:
 *   heap_start - Address of the beginning of the (initial) memory region.
 *   heap_size  - The size (in bytes) if the (initial) memory region.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  /* These values come from the linker scripts
   * (<chip>_<legacy/mcuboot>_sections.ld and <chip>_flat_memory.ld).
   * Check boards/risc-v/espressif.
   */

  board_autoled_on(LED_HEAPALLOCATE);

  *heap_start = (void *)g_idle_topstack;
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  /* v0/v1 SRAM is split. ROM reserved_start can be unmapped or past the
   * sram_low hole; clamp to the bootloader-reserved top of sram_low.
   */

  {
    uintptr_t heap_end = 0x4ff2cbd0;

    if (ets_rom_layout_p != NULL)
      {
        uintptr_t rom_end =
          (uintptr_t)ets_rom_layout_p->dram0_rtos_reserved_start;

        if (rom_end > g_idle_topstack && rom_end < heap_end)
          {
            heap_end = rom_end;
          }
      }

    *heap_size = heap_end - g_idle_topstack;
  }
#else
  *heap_size  = (uintptr_t)ets_rom_layout_p->dram0_rtos_reserved_start -
                           g_idle_topstack;
#endif
#ifdef CONFIG_ARCH_CHIP_ESP32P4
  ets_printf("heap %p size %u\n", *heap_start, (unsigned)*heap_size);
#endif
}

/****************************************************************************
 * Name: riscv_addregion
 *
 * Description:
 *   RAM may be added in non-contiguous chunks. This routine adds all chunks
 *   that may be used for heap.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#if CONFIG_MM_REGIONS > 1
void riscv_addregion(void)
{
#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  /* ESP32-P4 rev < v3 has non-contiguous SRAM: sram_low + sram_high.
   * The primary heap is in sram_low. Add sram_high as a second region.
   */

  extern uint8_t _sram_high_heap_start[];
  extern uint8_t _sram_high_heap_end[];

  size_t region_size = _sram_high_heap_end - _sram_high_heap_start;

  if (region_size > 0)
    {
#ifdef CONFIG_MM_KERNEL_HEAP
      kmm_addregion(_sram_high_heap_start, region_size);
#else
      kumm_addregion(_sram_high_heap_start, region_size);
#endif
    }
#endif
}
#endif

