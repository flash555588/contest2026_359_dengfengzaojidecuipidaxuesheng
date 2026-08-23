/****************************************************************************
 * arch/risc-v/src/common/espressif/esp_start.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/init.h>

#include "riscv_internal.h"

#include "esp_irq.h"
#include "esp_libc_stubs.h"
#include "esp_lowputc.h"
#include "esp_start.h"

#include "esp_rom_sys.h"
#include "esp_clk_internal.h"
#include "esp_private/rtc_clk.h"
#include "esp_cpu.h"
#include "esp_private/esp_mmu_map_private.h"
#include "esp_private/brownout.h"
#include "hal/wdt_hal.h"
#include "hal/mmu_hal.h"
#include "hal/mmu_types.h"
#include "hal/cache_types.h"
#include "hal/cache_ll.h"
#include "hal/cache_hal.h"
#include "hal/rwdt_ll.h"
#include "soc/ext_mem_defs.h"
#include "soc/reg_base.h"
#include "spi_flash_mmap.h"
#include "rom/cache.h"
#include "soc/soc.h"
#include "soc/soc_caps.h"
#ifdef CONFIG_ARCH_CHIP_ESP32P4
#  include "soc/hp_peri_pms_reg.h"
#  include "soc/lp_peri_pms_reg.h"
#  include "soc/timer_group_reg.h"
#  include "soc/lp_wdt_reg.h"
#  include "esp_private/regi2c_ctrl.h"
#  include "soc/regi2c_bias.h"
#  include "hal/regi2c_ctrl_ll.h"
#  include "esp_ldo.h"
#endif
#include "soc/rtc.h"

#include "bootloader_init.h"
#include "bootloader_sha.h"

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
#include "esp_rom_serial_output.h"
#include "esp_app_format.h"
#endif

#include "bootloader_mem.h"
#include "bootloader_flash_priv.h"
#include "esp_private/startup_internal.h"
#include "esp_private/spi_flash_os.h"
#ifdef CONFIG_ESPRESSIF_SPIRAM
#  include "esp_psram.h"
#  include "esp_private/esp_psram_extram.h"
#endif

#if SOC_APM_SUPPORTED
#  include "hal/apm_hal.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_DEBUG_FEATURES
#  define showprogress(c)     esp_rom_printf(c)
#else
#  define showprogress(c)
#endif

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
#  ifdef CONFIG_DEBUG_FEATURES
#    define sb_progress(s)    ets_printf(s)
#  else
#    define sb_progress(s)
#  endif
#else
#  define sb_progress(s)
#endif

/* Temporary ESP32-P4 Simple-Boot breadcrumbs.  Keep these enabled for both
 * revisions while the v1.x startup path is being brought up; they use only
 * the ROM console and make a silent clock/PSRAM hang immediately visible.
 */

#if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT) && \
    defined(CONFIG_ARCH_CHIP_ESP32P4)
#  define v3_progress(s)      ets_printf(s)
#else
#  define v3_progress(s)
#endif

#if defined(CONFIG_ESPRESSIF_BOOTLOADER_MCUBOOT) || \
    defined (CONFIG_ESPRESSIF_SIMPLE_BOOT)
#  ifdef CONFIG_ESPRESSIF_BOOTLOADER_MCUBOOT
#    define PRIMARY_SLOT_OFFSET   CONFIG_ESPRESSIF_OTA_PRIMARY_SLOT_OFFSET
#  else
#    define PRIMARY_SLOT_OFFSET   0  /* Force offset to the beginning of the whole image */
#  endif
#  define HDR_ATTR              __attribute__((section(".entry_addr"))) \
                                __attribute__((used))
#  define CHECKSUM_ALIGN        16
#  define IS_PADD(addr) ((addr) == 0)
#if defined(SOC_TCM_LOW) || defined(SOC_TCM_HIGH)
#  define IS_TCM(addr)  ((addr) >= SOC_TCM_LOW && (addr) < SOC_TCM_HIGH)
#else
#  define IS_TCM(addr) false
#endif
#  define IS_DRAM(addr) ((addr) >= SOC_DRAM_LOW && (addr) < SOC_DRAM_HIGH)
#  define IS_IRAM(addr) ((addr) >= SOC_IRAM_LOW && (addr) < SOC_IRAM_HIGH)
#  define IS_IROM(addr) ((addr) >= SOC_IROM_LOW && (addr) < SOC_IROM_HIGH)
#  define IS_DROM(addr) ((addr) >= SOC_DROM_LOW && (addr) < SOC_DROM_HIGH)
#  define IS_SRAM(addr) (IS_TCM(addr) || IS_IRAM(addr) || IS_DRAM(addr))
#  define IS_MMAP(addr) (IS_IROM(addr) || IS_DROM(addr))
#  ifdef SOC_RTC_FAST_MEM_SUPPORTED
#    define IS_RTC_FAST_IRAM(addr) \
                        ((addr) >= SOC_RTC_IRAM_LOW \
                         && (addr) < SOC_RTC_IRAM_HIGH)
#    define IS_RTC_FAST_DRAM(addr) \
                        ((addr) >= SOC_RTC_DRAM_LOW \
                         && (addr) < SOC_RTC_DRAM_HIGH)
#  else
#    define IS_RTC_FAST_IRAM(addr) false
#    define IS_RTC_FAST_DRAM(addr) false
#  endif
#  ifdef SOC_RTC_SLOW_MEM_SUPPORTED
#    define IS_RTC_SLOW_DRAM(addr) \
                        ((addr) >= SOC_RTC_DATA_LOW \
                         && (addr) < SOC_RTC_DATA_HIGH)
#  else
#    define IS_RTC_SLOW_DRAM(addr) false
#  endif
#  define IS_NONE(addr) (!IS_IROM(addr) \
                         && !IS_DROM(addr) \
                         && !IS_IRAM(addr) \
                         && !IS_TCM(addr) \
                         && !IS_DRAM(addr) \
                         && !IS_RTC_FAST_IRAM(addr) \
                         && !IS_RTC_FAST_DRAM(addr) \
                         && !IS_RTC_SLOW_DRAM(addr) \
                         && !IS_PADD(addr))

#  define IS_MAPPING(addr) IS_IROM(addr) || IS_DROM(addr)
#endif

#define NAPOT_RWX   (PMPCFG_A_NAPOT | PMPCFG_RWX_MASK)

/****************************************************************************
 * Private Types
 ****************************************************************************/

#if defined(CONFIG_ESPRESSIF_BOOTLOADER_MCUBOOT) || \
    defined (CONFIG_ESPRESSIF_SIMPLE_BOOT)
extern uint8_t _image_irom_vma[];
extern uint8_t _image_irom_lma[];
extern uint8_t _image_irom_size[];

extern uint8_t _image_drom_vma[];
extern uint8_t _image_drom_lma[];
extern uint8_t _image_drom_size[];
#endif

extern int _vector_table;

#if SOC_INT_CLIC_SUPPORTED
extern int _mtvt_table;
#endif

/****************************************************************************
 * ROM Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_ESPRESSIF_BOOTLOADER_MCUBOOT) || \
    defined (CONFIG_ESPRESSIF_SIMPLE_BOOT)
extern int ets_printf(const char *fmt, ...) printf_like(1, 2);
#endif

extern void cache_set_idrom_mmu_size(uint32_t irom_size, uint32_t drom_size);
extern void ets_delay_us(uint32_t us);

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_ESPRESSIF_BOOTLOADER_MCUBOOT) || \
    defined (CONFIG_ESPRESSIF_SIMPLE_BOOT)
IRAM_ATTR noreturn_function void __start(void);
#endif

#ifdef CONFIG_ARCH_CHIP_ESP32P4
/* ROM SPI_FAST_FLASH_BOOT arms TG0 MWDT and LP WDT flashboot independently
 * of WDT_EN. The timeout is far shorter than the 9s bootloader WDT, so
 * disable them with MMIO before installing NuttX vectors.
 */

#define ESP32P4_WDT_WKEY  0x50D83AA1

static inline void esp32p4_disable_flashboot_wdts(void)
{
  REG_WRITE(TIMG_WDTWPROTECT_REG(0), ESP32P4_WDT_WKEY);
  REG_CLR_BIT(TIMG_WDTCONFIG0_REG(0), TIMG_WDT_FLASHBOOT_MOD_EN);
  REG_CLR_BIT(TIMG_WDTCONFIG0_REG(0), TIMG_WDT_EN);
  REG_WRITE(TIMG_WDTWPROTECT_REG(0), 0);

  REG_WRITE(LP_WDT_WPROTECT_REG, ESP32P4_WDT_WKEY);
  REG_CLR_BIT(LP_WDT_CONFIG0_REG, LP_WDT_WDT_FLASHBOOT_MOD_EN);
  REG_CLR_BIT(LP_WDT_CONFIG0_REG, LP_WDT_WDT_EN);
  REG_WRITE(LP_WDT_WPROTECT_REG, 0);

  REG_WRITE(LP_WDT_SWD_WPROTECT_REG, ESP32P4_WDT_WKEY);
  REG_SET_BIT(LP_WDT_SWD_CONFIG_REG, LP_WDT_SWD_AUTO_FEED_EN);
  REG_WRITE(LP_WDT_SWD_WPROTECT_REG, 0);
}
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if defined(CONFIG_ESPRESSIF_BOOTLOADER_MCUBOOT) || \
    defined (CONFIG_ESPRESSIF_SIMPLE_BOOT)
HDR_ATTR static void (*_entry_point)(void) = __start;
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern uint8_t _instruction_reserved_start[];
extern uint8_t _instruction_reserved_end[];
extern uint8_t _rodata_reserved_start[];
extern uint8_t _rodata_reserved_end[];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: map_rom_segments
 *
 * Description:
 *   Configure the MMU and Cache peripherals for accessing ROM code and data.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#if defined(CONFIG_ESPRESSIF_BOOTLOADER_MCUBOOT) || \
    defined(CONFIG_ESPRESSIF_SIMPLE_BOOT)
static int map_rom_segments(uint32_t app_drom_start, uint32_t app_drom_vaddr,
                            uint32_t app_drom_size, uint32_t app_irom_start,
                            uint32_t app_irom_vaddr, uint32_t app_irom_size)
{
  uint32_t rc = 0;
  uint32_t actual_mapped_len = 0;
  uint32_t app_irom_start_aligned = app_irom_start & MMU_FLASH_MASK;
  uint32_t app_irom_vaddr_aligned = app_irom_vaddr & MMU_FLASH_MASK;
  uint32_t app_drom_start_aligned = app_drom_start & MMU_FLASH_MASK;
  uint32_t app_drom_vaddr_aligned = app_drom_vaddr & MMU_FLASH_MASK;
#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  esp_image_header_t image_header; /* Header for entire image */
  esp_image_segment_header_t WORD_ALIGNED_ATTR segment_hdr;
  bool padding_checksum = false;
  unsigned int segments = 0;
  unsigned int ram_segments = 0;
  unsigned int rom_segments = 0;
  size_t offset = CONFIG_BOOTLOADER_OFFSET_IN_FLASH;
#endif

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT

  /* Read image header */

  if (bootloader_flash_read(offset, &image_header,
                            sizeof(esp_image_header_t),
                            true) != ESP_OK)
    {
      ets_printf("Failed to load image header!\n");
      abort();
    }

  offset += sizeof(esp_image_header_t);

  /* Iterate for segment information parsing */

  while (segments++ < 16 && rom_segments < 2)
    {
      /* Read segment header */

      if (bootloader_flash_read(offset, &segment_hdr,
                                sizeof(esp_image_segment_header_t),
                                true) != ESP_OK)
        {
          ets_printf("failed to read segment header at %x\n", offset);
          abort();
        }

      if (IS_NONE(segment_hdr.load_addr))
        {
          /* Total segment count = (segments - 1) */

          break;
        }

      if (IS_RTC_FAST_IRAM(segment_hdr.load_addr) ||
          IS_RTC_FAST_DRAM(segment_hdr.load_addr) ||
          IS_RTC_SLOW_DRAM(segment_hdr.load_addr))
        {
          /* RTC segment is loaded by ROM bootloader */

          ram_segments++;
        }

      ets_printf("%s: lma 0x%08x vma 0x%08lx len 0x%-6lx (%lu)\n",
          IS_NONE(segment_hdr.load_addr) ? "???" :
            IS_RTC_FAST_IRAM(segment_hdr.load_addr) ||
            IS_RTC_FAST_DRAM(segment_hdr.load_addr) ||
            IS_RTC_SLOW_DRAM(segment_hdr.load_addr) ? "rtc" :
              IS_MMAP(segment_hdr.load_addr) ?
                IS_IROM(segment_hdr.load_addr) ? "imap" : "dmap" :
                  IS_PADD(segment_hdr.load_addr) ? "padd" :
                    IS_TCM(segment_hdr.load_addr) ? "tcm" :
                      IS_DRAM(segment_hdr.load_addr) ? "dram" : "iram",
          offset + sizeof(esp_image_segment_header_t),
          segment_hdr.load_addr, segment_hdr.data_len,
          segment_hdr.data_len);

      /* Fix drom and irom produced be the linker, as this
       * is later invalidated by the elf2image command.
       */

      if (IS_DROM(segment_hdr.load_addr) &&
          segment_hdr.load_addr == (uint32_t)_image_drom_vma)
        {
          app_drom_start = offset + sizeof(esp_image_segment_header_t);
          app_drom_start_aligned = app_drom_start & MMU_FLASH_MASK;
          rom_segments++;
        }

      if (IS_IROM(segment_hdr.load_addr) &&
          segment_hdr.load_addr == (uint32_t)_image_irom_vma)
        {
          app_irom_start = offset + sizeof(esp_image_segment_header_t);
          app_irom_start_aligned = app_irom_start & MMU_FLASH_MASK;
          rom_segments++;
        }

      if (IS_SRAM(segment_hdr.load_addr))
        {
          ram_segments++;
        }

      offset += sizeof(esp_image_segment_header_t) + segment_hdr.data_len;
      if (ram_segments == image_header.segment_count && !padding_checksum)
        {
          offset += (CHECKSUM_ALIGN - 1) - (offset % CHECKSUM_ALIGN) + 1;
          padding_checksum = true;
        }
    }

  if (segments == 0 || segments == 16)
    {
      ets_printf("Error parsing segments\n");
    }

  ets_printf("total segments stored %d\n", segments - 1);
#endif

#if defined(CONFIG_ARCH_CHIP_ESP32P4) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  ets_printf("M0\n");
#endif
  cache_hal_disable(CACHE_LL_LEVEL_EXT_MEM, CACHE_TYPE_ALL);
#if defined(CONFIG_ARCH_CHIP_ESP32P4) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  ets_printf("M1\n");
#endif

  /* Clear the MMU entries that are already set up,
   * so the new app only has the mappings it creates.
   */

  mmu_hal_unmap_all();
#if defined(CONFIG_ARCH_CHIP_ESP32P4) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  ets_printf("M2\n");
#endif

  mmu_hal_map_region(0, MMU_TARGET_FLASH0,
                     app_drom_vaddr_aligned, app_drom_start_aligned,
                     app_drom_size, &actual_mapped_len);
#if defined(CONFIG_ARCH_CHIP_ESP32P4) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  ets_printf("M3 len=%lu\n", (unsigned long)actual_mapped_len);
#endif

  mmu_hal_map_region(0, MMU_TARGET_FLASH0,
                     app_irom_vaddr_aligned, app_irom_start_aligned,
                     app_irom_size, &actual_mapped_len);
#if defined(CONFIG_ARCH_CHIP_ESP32P4) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  ets_printf("M4 len=%lu\n", (unsigned long)actual_mapped_len);
#endif

  /* ------------------Enable corresponding buses--------------------- */

  cache_bus_mask_t bus_mask = cache_ll_l1_get_bus(0, app_drom_vaddr_aligned,
                                                  app_drom_size);
  cache_ll_l1_enable_bus(0, bus_mask);
  bus_mask = cache_ll_l1_get_bus(0, app_irom_vaddr_aligned, app_irom_size);
  cache_ll_l1_enable_bus(0, bus_mask);
#if CONFIG_ESPRESSIF_NUM_CPUS > 1
  bus_mask = cache_ll_l1_get_bus(1, app_drom_vaddr_aligned, app_drom_size);
  cache_ll_l1_enable_bus(1, bus_mask);
  bus_mask = cache_ll_l1_get_bus(1, app_irom_vaddr_aligned, app_irom_size);
  cache_ll_l1_enable_bus(1, bus_mask);
#endif

#if defined(CONFIG_ARCH_CHIP_ESP32P4) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  ets_printf("M5\n");
#endif
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
  cache_ll_invalidate_addr(CACHE_LL_LEVEL_ALL, CACHE_TYPE_ALL,
                           CACHE_LL_ID_ALL, app_irom_vaddr_aligned,
                           actual_mapped_len);
#endif
#if defined(CONFIG_ARCH_CHIP_ESP32P4) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  ets_printf("M6\n");
#endif

  /* ------------------Enable Cache----------------------------------- */

  cache_hal_enable(CACHE_LL_LEVEL_EXT_MEM, CACHE_TYPE_ALL);
#if defined(CONFIG_ARCH_CHIP_ESP32P4) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  ets_printf("M7\n");
#endif

  return (int)rc;
}
#endif

/****************************************************************************
 * Name: recalib_bbpll
 *
 * Description:
 *   Workaround for bootloader calibration issues. This function is placed in
 *   IRAM because disabling BBPLL may influence the cache.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#if defined(CONFIG_ARCH_CHIP_ESP32C6) || defined(CONFIG_ARCH_CHIP_ESP32H2)
static void IRAM_ATTR NOINLINE_ATTR recalib_bbpll(void)
{
    rtc_cpu_freq_config_t old_config;
    rtc_clk_cpu_freq_get_config(&old_config);

  if (old_config.source == SOC_CPU_CLK_SRC_PLL
#ifdef CONFIG_ARCH_CHIP_ESP32H2
      || old_config.source == SOC_CPU_CLK_SRC_FLASH_PLL
#endif
      )
    {
      rtc_clk_cpu_freq_set_xtal();
      rtc_clk_cpu_freq_set_config(&old_config);
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

extern void esp_chip_revision_check(void);

/****************************************************************************
 * Name: riscv_soc_initialize
 *
 * Description:
 *   Initialize SoC-specific initialization.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void weak_function riscv_soc_initialize(void)
{
  sys_startup_fn();
}

/****************************************************************************
 * Name: sys_startup_fn
 *
 * Description:
 *   Execute the system layer startup function for the current CPU core.
 *   This function calls the appropriate startup function from the per-CPU
 *   startup function array (g_startup_fn) based on the current core ID.
 *   The SYS_STARTUP_FN() macro retrieves the core ID, indexes into the
 *   g_startup_fn array, and invokes the corresponding startup function.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void sys_startup_fn(void)
{
  SYS_STARTUP_FN();
}

/****************************************************************************
 * Name: __esp_start
 ****************************************************************************/

void __esp_start(void)
{
  esp_err_t ret;

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N1\n");
#ifdef CONFIG_ARCH_CHIP_ESP32P4
  esp32p4_disable_flashboot_wdts();
  sb_progress("N1w\n");
#endif
#endif

  esp_cpu_intr_set_ivt_addr(&_vector_table);

#if SOC_INT_CLIC_SUPPORTED
  /* When hardware vectored interrupts are enabled in CLIC,
   * the CPU jumps to this base address + 4 * interrupt_id.
   */

  esp_cpu_intr_set_mtvt_addr(&_mtvt_table);
#endif

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N1v\n");
#endif

#ifdef CONFIG_ESP_ROM_NEEDS_SET_CACHE_MMU_SIZE
  uint32_t _instruction_size;
  uint32_t cache_mmu_irom_size;
#endif

  bootloader_clear_bss_section();

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N1b\n");
  if (bootloader_init() != 0)
    {
      ets_printf("Hardware init failed, aborting\n");
      while (true);
    }

  sb_progress("N2\n");
#endif

  /* Initialize the per CPU areas */

#ifdef CONFIG_RISCV_PERCPU_SCRATCH
  riscv_percpu_add_hart(0);
#endif

#if defined(CONFIG_ESPRESSIF_BOOTLOADER_MCUBOOT) || \
    defined(CONFIG_ESPRESSIF_SIMPLE_BOOT)
  size_t partition_offset = PRIMARY_SLOT_OFFSET;
  uint32_t app_irom_start = partition_offset + (uint32_t)_image_irom_lma;
  uint32_t app_irom_size  = (uint32_t)_image_irom_size;
  uint32_t app_irom_vaddr = (uint32_t)_image_irom_vma;
  uint32_t app_drom_start = partition_offset + (uint32_t)_image_drom_lma;
  uint32_t app_drom_size  = (uint32_t)_image_drom_size;
  uint32_t app_drom_vaddr = (uint32_t)_image_drom_vma;

  if (map_rom_segments(app_drom_start, app_drom_vaddr, app_drom_size,
                       app_irom_start, app_irom_vaddr, app_irom_size) != 0)
    {
      ets_printf("Failed to setup XIP, aborting\n");
      while (true);
    }

  v3_progress("V0\n");

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N3\n");
#endif
#endif

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N3a\n");
#endif

#if CONFIG_ESP_ROM_NEEDS_SET_CACHE_MMU_SIZE
  v3_progress("V1\n");
  _instruction_size = (uint32_t)&_instruction_reserved_end - \
                      (uint32_t)&_instruction_reserved_start;
  cache_mmu_irom_size =
      ((_instruction_size + SPI_FLASH_MMU_PAGE_SIZE - 1) / \
      SPI_FLASH_MMU_PAGE_SIZE) * sizeof(uint32_t);

  /* Configure the Cache MMU size for instruction and rodata in flash. */

  cache_set_idrom_mmu_size(cache_mmu_irom_size,
                           CACHE_DROM_MMU_MAX_END - cache_mmu_irom_size);
  v3_progress("V2\n");
#endif /* CONFIG_ESP_ROM_NEEDS_SET_CACHE_MMU_SIZE */

#if CONFIG_ESP_SYSTEM_BBPLL_RECALIB
  recalib_bbpll();
#endif

#ifdef CONFIG_ESPRESSIF_REGION_PROTECTION
  /* Configure region protection */

  esp_cpu_configure_region_protection();
#endif

  v3_progress("V3\n");

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N3p\n");
#endif

  /* Configure the power related stuff. */

  v3_progress("V4\n");
  esp_rtc_init();
  v3_progress("V5\n");

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N3r\n");
#endif

  v3_progress("V6\n");
  esp_mspi_pin_init();

  /* Configure SPI Flash chip state */

  spi_flash_init_chip_state();

  esp_mmu_map_init();
  v3_progress("V7\n");

#if defined(CONFIG_ESPRESSIF_SPIRAM) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  /* ESP32-P4 v3.x uses the complete IDF startup path.  Initialize PSRAM
   * before the clock switch, matching the upstream ESP32-P4 sequence. */

  v3_progress("V8\n");
  ret = esp_psram_chip_init();
#  if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT)
  ets_printf("V9 ret=%ld\n", (long)ret);
#  endif
  if (ret != ESP_OK)
    {
#  ifndef CONFIG_ESPRESSIF_SPIRAM_IGNORE_NOTFOUND
      PANIC();
#  endif
    }

#  ifdef CONFIG_ESPRESSIF_SPIRAM_BOOT_INIT
  if (ret == ESP_OK)
    {
      ret = esp_psram_init();
#    if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT)
      ets_printf("VA ret=%ld\n", (long)ret);
#    endif
      if (ret != ESP_OK)
        {
#    ifndef CONFIG_ESPRESSIF_SPIRAM_IGNORE_NOTFOUND
          PANIC();
#    endif
        }
    }
#  endif
#endif

  v3_progress("VB\n");

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N3m\n");
#endif

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  /* P4 v1.0 minimal clock bring-up (Simple Boot).
   *
   * The full esp_clk_init() cannot run in Simple Boot until the complete
   * IDF analog bring-up (bootloader_hardware_init: regi2c master init,
   * PMU/PVT sequence) has been restored; without it CPLL@360MHz operation
   * is marginal and XIP instruction fetches corrupt at random. Measured on
   * ESP32-P4 rev v1.0 (Function-EV-Board v1.4): any esp_clk_init() path
   * crashes within seconds, staying on ROM clocks or raising the CPU to
   * CPLL@90MHz only is stable. See logs/flash555588 work record.
   *
   * Therefore:
   *  1. keep the analog i2c master clock gated on and apply the 1.1V bias
   *     trims IDF applies before raising any clock;
   *  2. raise the CPU to CPLL@90MHz only - the lowest entry of the
   *     CONFIG_ESP32P4_SELECTS_REV_LESS_V3 divider table - verified stable.
   * RTC slow/fast source selection and RC_FAST are left as ROM defaults;
   * NuttX keeps time via the systimer, which runs from XTAL.
   *
   * TODO(P2): restore full bootloader_hardware_init() sequence, then
   * validate 180/360MHz and re-enable esp_clk_init().
   */
  _regi2c_ctrl_ll_master_enable_clock(true);
  regi2c_ctrl_ll_master_configure_clock();
  REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1, 10);
  REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1_PVT, 10);

  /* Board carries a 40MHz crystal. Persist it into the LP store register:
   * Simple Boot skips the bootloader step that would do this, and every
   * rtc_clk_xtal_freq_get() otherwise warns "invalid RTC_XTAL_FREQ_REG"
   * and falls back to the same 40MHz assumption. */
  rtc_clk_xtal_freq_update(SOC_XTAL_FREQ_40M); /* XTAL_FREQ_UPDATE_DONE */
#endif /* CONFIG_ESP32P4_SELECTS_REV_LESS_V3 */


  /* Configures the CPU clock, RTC slow and fast clocks, and performs
   * RTC slow clock calibration.
   */

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
#  ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  {
    rtc_cpu_freq_config_t newc;

    v3_progress("RC0\n");
    if (rtc_clk_cpu_freq_mhz_to_config(90, &newc))
      {
        rtc_clk_cpu_freq_set_config(&newc);
      }

    v3_progress("RC1\n");
  }

#ifdef CONFIG_ESPRESSIF_SPIRAM
  /* Silent early PSRAM bring-up: MUST run before the kernel heap is
   * initialised so up_allocate_heap() can kumm_addregion() the PSRAM
   * range (CONFIG_ESPRESSIF_SPIRAM_USER_HEAP). No logging here - the
   * ROM console is dead after the MSPI clock switch; board bring-up
   * reports the outcome on the live console instead. */
  {
    struct esp_ldo_config_t psram_ldo = { 0 };
    extern int   esp_ldo_channel_acquire(struct esp_ldo_config_t *config);
    extern int   esp_psram_chip_init(void);
    extern int   esp_psram_init(void);
    int pret = -1;

    psram_ldo.chan_id    = 2;      /* VDD_PSRAM domain */
    psram_ldo.voltage_mv = 1800;
    psram_ldo.handler    = NULL;

    pret = esp_ldo_channel_acquire(&psram_ldo);
    v3_progress("RP0\n");
    if (pret == OK)
      {
        pret = esp_psram_chip_init();
        v3_progress("RPC\n");
        if (pret == 0)
          {
            pret = esp_psram_init();
            ets_printf("RPP ret=%ld\n", (long)pret);
          }
      }

    ets_printf("RPE ret=%ld\n", (long)pret);
  }
#  endif

#  else
  /* On ESP32-P4 ECO7 the ROM Simple-Boot path has already established a
   * working CPU/RTC clock tree.  Re-running the full IDF esp_clk_init()
   * sequence blocks before returning (VC without VD on the UART trace),
   * just as the duplicated bootloader analog initialization did earlier.
   * Preserve the live ROM clock state; peripheral drivers derive their
   * clocks from the hardware state during normal NuttX bring-up.
   */
  v3_progress("VC\n");
  v3_progress("VD\n");
#  endif
#else
  esp_clk_init();
#endif /* CONFIG_ESPRESSIF_SIMPLE_BOOT */

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N4\n");
#endif

  /* P2-3: reserve MSPI pins so the GPIO driver cannot hand them out.
   * Pure bookkeeping (esp_gpio_reserve), safe in Simple Boot too. */
  esp_mspi_pin_reserve();
  v3_progress("VE\n");

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N4a\n");
#endif

#if !defined(CONFIG_ESPRESSIF_SIMPLE_BOOT) || \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  v3_progress("VF\n");
  bootloader_init_mem();
  v3_progress("VG\n");
#endif

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N4b\n");
#endif

#ifdef CONFIG_ESPRESSIF_SPIRAM_MEMTEST
  if (esp_psram_is_initialized() && !esp_psram_extram_test())
    {
      PANIC();
    }
#endif

#ifdef CONFIG_ESPRESSIF_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
  esp_psram_bss_init();
#endif

  v3_progress("VH\n");

  /* Disable clock of unused peripherals */

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  /* P4 v1.x Simple Boot: keep peripheral clocks unchanged; disabling the
   * SMEM clock here destabilizes the validated legacy PSRAM path. */
#else
  v3_progress("VI\n");
  esp_perip_clk_init();
  v3_progress("VJ\n");
#endif

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N4c\n");
#endif

#if defined(CONFIG_ESPRESSIF_BROWNOUT_DET)
  /* Initialize hardware brownout check and reset (P2-3) */

  esp_brownout_init();
#endif

  v3_progress("VK\n");

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N4d\n");
#endif

  /* Configure the UART so we can get debug output */

  esp_lowsetup();
  v3_progress("VL\n");

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N5\n");
#endif

#ifdef USE_EARLYSERIALINIT
  /* Perform early serial initialization */

  riscv_earlyserialinit();
#endif

  v3_progress("VM\n");
  esp_chip_revision_check();
  v3_progress("VN\n");

  showprogress("A");

  /* Setup the syscall table needed by the ROM code */

  esp_setup_syscall_table();

  showprogress("B");

  /* The 2nd stage bootloader enables RTC WDT to monitor any issues that may
   * prevent the startup sequence from finishing correctly. Hence disable it
   * as NuttX is about to start.
   */

  wdt_hal_context_t rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();
  wdt_hal_write_protect_disable(&rwdt_ctx);
  wdt_hal_set_flashboot_en(&rwdt_ctx, false);
  wdt_hal_disable(&rwdt_ctx);
  wdt_hal_write_protect_enable(&rwdt_ctx);

  showprogress("C");

  /* Initialize onboard resources */

  esp_board_initialize();

  showprogress("D");

#ifdef CONFIG_ESPRESSIF_SIMPLE_BOOT
  sb_progress("N6\n");
#endif

  v3_progress("VO\n");

  nx_start();

  UNUSED(ret);

  for (; ; );
}
