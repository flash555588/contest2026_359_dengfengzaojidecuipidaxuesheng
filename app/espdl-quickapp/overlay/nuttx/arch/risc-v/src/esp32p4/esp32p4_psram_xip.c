/****************************************************************************
 * nuttx/arch/risc-v/src/esp32p4/esp32p4_psram_xip.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_private/esp_psram_extram.h"
#include "hal/mmu_hal.h"
#include "hal/mmu_ll.h"
#include "soc/ext_mem_defs.h"

#if !defined(CONFIG_SPIRAM_FETCH_INSTRUCTIONS) || \
    !defined(CONFIG_SPIRAM_RODATA) || \
    !defined(CONFIG_SPIRAM_FLASH_LOAD_TO_PSRAM)
#  error "P4 XIP requires both copied segments and the P4 CPU-copy implementation"
#endif

extern char _instruction_reserved_start[];
extern char _instruction_reserved_end[];
extern char _rodata_reserved_start[];
extern char _rodata_reserved_end[];
#ifdef CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
extern char _ext_ram_bss_start[];
extern char _ext_ram_bss_end[];
#endif
void image_process_get_flash_segments_info(uint32_t *, uint32_t *);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Singleton boot state, required before either heap is initialized. */

static bool g_xip_ready;
static uint32_t g_text_hash;
static uint32_t g_rodata_hash;
static atomic_uint g_flash_entries;
static atomic_uint g_protected_cpus;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t IRAM_ATTR xip_hash(uintptr_t at, size_t bytes)
{
  volatile const uint8_t *p = (volatile const uint8_t *)at;
  uint32_t hash = 2166136261u;
  while (bytes-- != 0)
    {
      hash = (hash ^ *p++) * 16777619u;
    }

  return hash;
}

static void IRAM_ATTR xip_fail(void)
{
  static DRAM_ATTR const char message[] =
    "[psram-xip] verification FAILED\n";

  esp_rom_printf(message);
  abort();
}

static uint32_t IRAM_ATTR xip_verify_segment(uintptr_t first, uintptr_t end,
                                            uint32_t physical,
                                            uint32_t flash_physical)
{
  const uint32_t page = CONFIG_MMU_PAGE_SIZE;
  uintptr_t aligned = first & ~(page - 1u);
  uintptr_t finish = (end + page - 1u) & ~(page - 1u);
  if (first >= end || aligned < SOC_DRAM_PSRAM_ADDRESS_LOW ||
      finish > SOC_DRAM_PSRAM_ADDRESS_HIGH)
    {
      xip_fail();
    }

  for (uintptr_t v = aligned; v < finish; v += page)
    {
      uint32_t paddr;
      mmu_target_t target;
      if (!mmu_hal_vaddr_to_paddr(MMU_LL_PSRAM_MMU_ID, v, &paddr, &target) ||
          target != MMU_TARGET_PSRAM0 || paddr != physical + v - aligned)
        {
          xip_fail();
        }
    }

  uint32_t hash = xip_hash(first, end - first);
  /* Simple Boot retains a read-only temporary Flash alias at VMA-0x08000000.
   * Compare every byte through independently mapped Flash/PSRAM buses.
   */

  if (hash != xip_hash(first - SOC_DRAM_FLASH_PSRAM_OFFSET, end - first))
    {
      xip_fail();
    }

  uint32_t paddr;
  mmu_target_t target;
  if (!mmu_hal_vaddr_to_paddr(MMU_LL_FLASH_MMU_ID,
                            first - SOC_DRAM_FLASH_PSRAM_OFFSET,
                            &paddr, &target) ||
      target != MMU_TARGET_FLASH0 ||
      paddr != flash_physical)
    {
      xip_fail();
    }

  return hash;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void IRAM_ATTR esp32p4_psram_xip_verify(void)
{
  const uint32_t page = CONFIG_MMU_PAGE_SIZE;
  uintptr_t text = (uintptr_t)_instruction_reserved_start;
  uintptr_t text_end = (uintptr_t)_instruction_reserved_end;
  uintptr_t rodata = (uintptr_t)_rodata_reserved_start;
  uintptr_t rodata_end = (uintptr_t)_rodata_reserved_end;
  uint32_t rodata_bytes = ((rodata_end + page - 1u) & ~(page - 1u)) -
                          (rodata & ~(page - 1u));
  uint32_t flash_rodata;
  uint32_t flash_text;
  image_process_get_flash_segments_info(&flash_rodata, &flash_text);
  g_rodata_hash = xip_verify_segment(rodata, rodata_end, 0, flash_rodata);
  g_text_hash = xip_verify_segment(text, text_end, rodata_bytes, flash_text);
  uintptr_t heap = esp_psram_extram_vaddr_start();
  uintptr_t heap_end = esp_psram_extram_vaddr_end();
  if (heap < rodata_end || heap < text_end || heap_end <= heap)
    {
      xip_fail();
    }

#ifdef CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
  if (heap < (uintptr_t)_ext_ram_bss_end ||
      (uintptr_t)_ext_ram_bss_start < rodata_end)
    {
      xip_fail();
    }

#endif
  g_xip_ready = true;
  esp_rom_printf("[psram-xip] verified text=%08lx..%08lx hash=%08lx "
                 "rodata=%08lx..%08lx hash=%08lx heap=%08lx..%08lx\n",
                 (unsigned long)text, (unsigned long)text_end,
                 (unsigned long)g_text_hash, (unsigned long)rodata,
                 (unsigned long)rodata_end, (unsigned long)g_rodata_hash,
                 (unsigned long)heap, (unsigned long)heap_end);
}

bool IRAM_ATTR esp32p4_psram_xip_flash_enter(void)
{
  if (!g_xip_ready ||
      !(atomic_load_explicit(&g_protected_cpus, memory_order_acquire) & 1))
    {
      return false;
    }

  atomic_fetch_add_explicit(&g_flash_entries, 1, memory_order_relaxed);
  return true;
}

#ifdef __riscv
void IRAM_ATTR esp32p4_psram_xip_protect_cpu(void)
{
  extern void esp_cpu_configure_region_protection(void);
  uintptr_t code_end;
  uintptr_t rodata_end;
  uintptr_t heap_end;
  uintptr_t cfg2;
  uintptr_t cfg3;
  uintptr_t cpu;

  if (!g_xip_ready)
    {
      xip_fail();
    }

  esp_cpu_configure_region_protection();
  __asm__ __volatile__("csrr %0, pmpaddr11" : "=r"(code_end));
  __asm__ __volatile__("csrr %0, pmpaddr13" : "=r"(rodata_end));
  __asm__ __volatile__("csrr %0, pmpaddr14" : "=r"(heap_end));
  __asm__ __volatile__("csrr %0, pmpcfg2" : "=r"(cfg2));
  __asm__ __volatile__("csrr %0, pmpcfg3" : "=r"(cfg3));
  __asm__ __volatile__("csrr %0, mhartid" : "=r"(cpu));
  if ((code_end << 2) != (uintptr_t)_instruction_reserved_end ||
      (rodata_end << 2) !=
        (((uintptr_t)_rodata_reserved_end + 127) & ~(uintptr_t)127) ||
      (heap_end << 2) != esp_psram_extram_vaddr_end() ||
      ((cfg2 >> 24) & 0x9f) != 0x8d ||
      ((cfg3 >> 8) & 0x9f) != 0x89 ||
      ((cfg3 >> 16) & 0x9f) != 0x8b || cpu > 1)
    {
      xip_fail();
    }

  atomic_fetch_or_explicit(&g_protected_cpus, 1u << cpu,
                           memory_order_release);
}
#endif

void esp32p4_psram_xip_status(void)
{
  printf("[psram-xip] ready=%u protected_cpus=%lu flash_entries=%lu "
         "text_hash=%08lx "
         "rodata_hash=%08lx cache_policy=on spi1_mutex=1\n",
         g_xip_ready,
         (unsigned long)atomic_load_explicit(&g_protected_cpus,
                                             memory_order_acquire),
         (unsigned long)atomic_load_explicit(&g_flash_entries,
                                             memory_order_relaxed),
         (unsigned long)g_text_hash, (unsigned long)g_rodata_hash);
}
