"""Opt-in P4 Simple Boot PSRAM XIP port for the prepared NuttX/HAL tree.

The normal build keeps this disabled. Copying uses Espressif's P4 v2 loader;
the NuttX bridge supplies the segment offsets parsed from the actual image.
"""
from pathlib import Path
import hashlib
from protect_p4_display_flash import once

KCONFIG = '''
# ESPCLAW P4 PSRAM XIP
config ESP32P4_PSRAM_XIP
    bool "Experimental complete P4 PSRAM text/rodata XIP"
    default n
    depends on ESPRESSIF_SPIRAM_BOOT_INIT && ESPRESSIF_SIMPLE_BOOT
    depends on !ESP32P4_SELECTS_REV_LESS_V3 && !ESPRESSIF_SPIRAM_IGNORE_NOTFOUND
    select SPIRAM_FETCH_INSTRUCTIONS
    select SPIRAM_RODATA
    select SPIRAM_XIP_FROM_PSRAM
    select SPIRAM_FLASH_LOAD_TO_PSRAM

config SPIRAM_FETCH_INSTRUCTIONS
    bool
config SPIRAM_RODATA
    bool
config SPIRAM_XIP_FROM_PSRAM
    bool
config SPIRAM_FLASH_LOAD_TO_PSRAM
    bool
'''

def apply(nuttx):
    root=Path(nuttx)
    sdk=root/'arch/risc-v/src/esp32p4/esp-hal-3rdparty'
    common=root/'arch/risc-v/src/common/espressif'
    pending={}
    kconfig=root/'arch/risc-v/src/esp32p4/Kconfig'
    s=kconfig.read_text()
    if '# ESPCLAW P4 PSRAM XIP' not in s:s+='\n'+KCONFIG
    pending[kconfig]=s

    start=common/'esp_start.c';s=start.read_text()
    if 'ESPCLAW P4 PSRAM XIP' not in s:
        anchor='#  define IS_IROM(addr) ((addr) >= SOC_IROM_LOW && (addr) < SOC_IROM_HIGH)\n#  define IS_DROM(addr) ((addr) >= SOC_DROM_LOW && (addr) < SOC_DROM_HIGH)'
        s=once(s,anchor,'''/* ESPCLAW P4 PSRAM XIP: ROM skips these mapped image segments;
 * P4 PSRAM uses a distinct address window, unlike S3. */
#ifdef CONFIG_ESP32P4_PSRAM_XIP
#  define IS_IROM(addr) SOC_ADDRESS_IN_IRAM_PSRAM(addr)
#  define IS_DROM(addr) SOC_ADDRESS_IN_DRAM_PSRAM(addr)
#else
'''+anchor+'\n#endif','XIP image classification')
        # ext_mem_defs names its shared instruction/data PSRAM range using DRAM.
        s=s.replace('SOC_ADDRESS_IN_IRAM_PSRAM(addr)','SOC_ADDRESS_IN_DRAM_PSRAM(addr)')
        anchor='static int map_rom_segments('
        bridge='''#ifdef CONFIG_ESP32P4_PSRAM_XIP
/* Populated by Simple Boot image parsing before esp_psram_init(). These are
 * physical offsets in the whole Flash, including the actual 0x2000 origin. */
static uint32_t g_xip_flash_drom;
static uint32_t g_xip_flash_irom;
void image_process_get_flash_segments_info(uint32_t *drom, uint32_t *irom)
{
  *drom = g_xip_flash_drom;
  *irom = g_xip_flash_irom;
}
#endif

'''
        s=once(s,anchor,bridge+anchor,'NuttX image offset bridge')
        anchor='  cache_hal_disable(CACHE_LL_LEVEL_EXT_MEM, CACHE_TYPE_ALL);'
        s=once(s,anchor,'''#ifdef CONFIG_ESP32P4_PSRAM_XIP
  if (rom_segments != 2 || app_irom_start == 0 || app_drom_start == 0)
    {
      ets_printf("[psram-xip] invalid image segments\\n");
      abort();
    }
  g_xip_flash_drom = app_drom_start;
  g_xip_flash_irom = app_irom_start;
  /* Only the temporary Flash aliases can be mapped before PSRAM init.
   * All code/data reachable until the copies finish stays in internal RAM. */
  app_drom_vaddr_aligned -= SOC_DRAM_FLASH_PSRAM_OFFSET;
  app_irom_vaddr_aligned -= SOC_IRAM_FLASH_PSRAM_OFFSET;
#endif

'''+anchor,'temporary Flash aliases')
        anchor='  if (ret == ESP_OK)\n    {\n      ret = esp_psram_init();'
        s=once(s,anchor,anchor+'''
#ifdef CONFIG_ESP32P4_PSRAM_XIP
      if (ret == ESP_OK)
        {
          extern void esp32p4_psram_xip_verify(void);
          esp32p4_psram_xip_verify();
        }
#endif''','verify mappings before leaving internal startup')
    pending[start]=s
    if 'ESPCLAW XIP PMP ORDER' not in s:
        anchor='#ifdef CONFIG_ESPRESSIF_REGION_PROTECTION\n  /* Configure region protection */\n\n  esp_cpu_configure_region_protection();\n#endif'
        s=once(s,anchor,'''/* ESPCLAW XIP PMP ORDER: lock RX/R permissions only after copying. */
#if defined(CONFIG_ESPRESSIF_REGION_PROTECTION) && !defined(CONFIG_ESP32P4_PSRAM_XIP)
  esp_cpu_configure_region_protection();
#endif''','defer CPU0 PMP lock')
        anchor='          esp32p4_psram_xip_verify();'
        s=once(s,anchor,anchor+'''
          extern void esp32p4_psram_xip_protect_cpu(void);
          esp32p4_psram_xip_protect_cpu();''','protect copied image before runtime')
    pending[start]=s
    boot=sdk/'components/bootloader_support/src/bootloader_mem.c';s=boot.read_text()
    if 'ESPCLAW XIP PMP ORDER' not in s:
        anchor='#if CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE'
        s=once(s,anchor,'''/* ESPCLAW XIP PMP ORDER: retain early APM setup above; defer only the
 * final application PMP lock until the PSRAM CPU-copy has completed. */
#if CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE && !defined(CONFIG_ESP32P4_PSRAM_XIP)''','defer Simple Boot PMP lock')
    pending[boot]=s
    smp=root/'arch/risc-v/src/esp32p4/esp32p4_smp.c';s=smp.read_text()
    if 'ESPCLAW XIP CPU1 PMP' not in s:
        anchor='  __atomic_store_n(&g_secondary_ready[1], true, __ATOMIC_RELEASE);'
        s=once(s,anchor,'''#ifdef CONFIG_ESP32P4_PSRAM_XIP
  /* ESPCLAW XIP CPU1 PMP: mappings/copies are ready before CPU1 release. */
  extern void esp32p4_psram_xip_protect_cpu(void);
  esp32p4_psram_xip_protect_cpu();
#endif
'''+anchor,'CPU1 final PMP before scheduling')
    pending[smp]=s
    protection=sdk/'components/esp_hw_support/port/esp32p4/cpu_region_protect.c';s=protection.read_text()
    if 'ESPCLAW XIP PMP HEAP END' not in s:
        anchor='    PMP_ENTRY_SET_CACHED_AND_UNCACHED(13, 18, (uint32_t)(&_rodata_reserved_end), PMP_TOR | R);\n    PMP_ENTRY_SET_CACHED_AND_UNCACHED(14, 19, ALIGN_UP((uint32_t)(&_rodata_reserved_end) + available_psram_heap, SOC_CPU_PMP_REGION_GRANULARITY), PMP_TOR | RW);'
        s=once(s,anchor,'''#ifdef CONFIG_ESP32P4_PSRAM_XIP
    /* ESPCLAW XIP PMP HEAP END: PSRAM heap begins at the next MMU page,
     * not immediately after rodata. Cover all allocated pages and protect
     * the last constant bytes even with a 128-byte PMP granule. */
    (void)available_psram_heap;
    const uint32_t ro_end = ((uint32_t)&_rodata_reserved_end + SOC_CPU_PMP_REGION_GRANULARITY - 1) & ~(SOC_CPU_PMP_REGION_GRANULARITY - 1);
    PMP_ENTRY_SET_CACHED_AND_UNCACHED(13, 18, ro_end, PMP_TOR | R);
    PMP_ENTRY_SET_CACHED_AND_UNCACHED(14, 19, esp_psram_extram_vaddr_end(), PMP_TOR | RW);
#else
'''+anchor+'\n#endif','P4 v3 actual heap end')
    pending[protection]=s

    scripts=root/'boards/risc-v/esp32p4/common/scripts'
    memory=scripts/'esp32p4_flat_memory.ld';s=memory.read_text()
    if 'ESPCLAW P4 PSRAM XIP' not in s:
        anchor='  irom_seg (RX) :                    org = 0x40000000, len = IDROM_SEG_SIZE'
        s=once(s,anchor,'''  /* ESPCLAW P4 PSRAM XIP: P4 has separate Flash and PSRAM MMUs. */
#ifdef CONFIG_ESP32P4_PSRAM_XIP
  irom_seg (RX) :                    org = 0x48000000, len = IDROM_SEG_SIZE
#else
'''+anchor+'\n#endif','P4 instruction VMA')
    pending[memory]=s
    sections=scripts/'esp32p4_sections.rev3.ld';s=sections.read_text()
    if 'ESPCLAW P4 PSRAM XIP' not in s:
        anchor='    *libarch.a:esp_psram.*(.literal.esp_psram_get_heap_size_to_protect .text.esp_psram_get_heap_size_to_protect)'
        s=once(s,anchor,anchor+'''
#ifdef CONFIG_ESP32P4_PSRAM_XIP
    /* ESPCLAW P4 PSRAM XIP: entire pre-copy dependency chain, not just leaves. */
    *libarch.a:esp_psram.*(.literal .literal.* .text .text.*)
    *libarch.a:esp_mmu_map.*(.literal .literal.* .text .text.*)
    *libarch.a:mmu_psram_flash_v2.*(.literal .literal.* .text .text.*)
#endif''','pre-copy internal code')
        anchor='    *libarch.a:esp_psram_impl_ap_hex.*(.rodata .rodata.*)'
        s=once(s,anchor,anchor+'''
#ifdef CONFIG_ESP32P4_PSRAM_XIP
    *libarch.a:esp_psram.*(.rodata .rodata.* .srodata .srodata.*)
    *libarch.a:esp_mmu_map.*(.rodata .rodata.* .srodata .srodata.*)
    *libarch.a:ext_mem_layout.*(.rodata .rodata.* .srodata .srodata.*)
    *libarch.a:mmu_psram_flash_v2.*(.rodata .rodata.* .srodata .srodata.*)
#endif''','pre-copy internal constants')
        begin=s.index('#ifdef CONFIG_ESPRESSIF_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY\n  /* This section holds .ext_ram.bss')
        end=s.index('\n  .flash.text :',begin)
        block=s[begin:end]
        s=s[:begin]+s[end:]
        # Shared PSRAM VMA: external BSS follows BOTH copied segments. The SDK
        # maps heap-eligible physical pages after the same two reservations.
        anchor='  _image_irom_vma = ADDR(.flash.text);'
        relocated=block.replace('    _ext_ram_bss_start = ABSOLUTE(.);','    . = ALIGN(0x10000);\n    _ext_ram_bss_start = ABSOLUTE(.);')
        s=once(s,anchor,relocated+'\n'+anchor,'external BSS after copied ranges')
        anchor='  _image_drom_vma = ADDR(.flash.rodata);'
        s=once(s,anchor,'  _image_drom_vma = ADDR(.flash.appdesc);','complete rodata segment start')
        anchor='  _image_drom_size = SIZEOF(.flash.rodata);'
        s=once(s,anchor,'  _image_drom_size = SIZEOF(.flash.appdesc) + SIZEOF(.flash.rodata);','complete rodata segment length')
    if 'ESPCLAW XIP EARLY DEPENDENCIES V2' not in s:
        anchor='    *libarch.a:mmu_psram_flash_v2.*(.literal .literal.* .text .text.*)'
        s=once(s,anchor,anchor+'''
    /* ESPCLAW XIP EARLY DEPENDENCIES V2 */
    *libarch.a:mspi_timing_tuning.*(.literal .literal.* .text .text.*)
    *libarch.a:esp_ldo_regulator.*(.literal .literal.* .text .text.*)
    *libarch.a:pmu_pvt.*(.literal .literal.* .text .text.*)
    *(.text.up_switch_context .text.nxsched_switch_running)''','remaining pre-copy code dependencies')
        anchor='    *libarch.a:mmu_psram_flash_v2.*(.rodata .rodata.* .srodata .srodata.*)'
        s=once(s,anchor,anchor+'''
    *libarch.a:mspi_timing_tuning.*(.rodata .rodata.* .srodata .srodata.*)
    *libarch.a:esp_ldo_regulator.*(.rodata .rodata.* .srodata .srodata.*)
    *libarch.a:pmu_pvt.*(.rodata .rodata.* .srodata .srodata.*)
    *libarch.a:clk_ctrl_os.*(.rodata .rodata.* .srodata .srodata.*)''','remaining pre-copy constants')
    pending[sections]=s
    if 'ESPCLAW XIP EARLY DEPENDENCIES V3' not in s:
        anchor='    *(.text.up_switch_context .text.nxsched_switch_running)'
        s=once(s,anchor,anchor+'''
    /* ESPCLAW XIP EARLY DEPENDENCIES V3: tuning callback targets and FPU. */
    *libarch.a:mspi_timing*.*(.literal .literal.* .text .text.*)
    *(.text.riscv_savefpu .text.riscv_restorefpu)''','tuning callback code')
        anchor='    *libarch.a:clk_ctrl_os.*(.rodata .rodata.* .srodata .srodata.*)'
        s=once(s,anchor,anchor+'''
    *libarch.a:mspi_timing*.*(.rodata .rodata.* .srodata .srodata.*)
    *libarch.a:sleep_modes.*(.rodata .rodata.* .srodata .srodata.*)''','tuning and sleep constants')
    pending[sections]=s
    if 'ESPCLAW XIP EARLY DEPENDENCIES V4' not in s:
        anchor='    *(.text.riscv_savefpu .text.riscv_restorefpu)'
        s=once(s,anchor,'''    /* ESPCLAW XIP EARLY DEPENDENCIES V4: FPU is one assembly .text. */
    *libarch.a:riscv_fpu.*(.literal .literal.* .text .text.*)''','FPU assembly object')
        for obj in ['esp_spiflash','esp_flash_api','spi_flash_chip*','spi_flash_hal_gpspi','spi_flash_os_func_app']:
            # These runtime Flash operations may execute from copied PSRAM.
            # Explicit IRAM_ATTR sections remain internal independently.
            import re
            pattern=r'^    \*libarch\.a:'+re.escape(obj)+r'\.\*\([^\n]*\)$'
            s,n=re.subn(pattern,lambda m:'#ifndef CONFIG_ESP32P4_PSRAM_XIP\n'+m[0]+'\n#endif',s,flags=re.M)
            if n==0:raise RuntimeError('Missing post-copy object '+obj)
        anchor='    *libarch.a:esp_mmu_map.*(.literal .literal.* .text .text.*)'
        s=once(s,anchor,'''    *libarch.a:esp_mmu_map.*(.text.esp_mmu_map_init*
      .text.esp_mmu_map_get_max_consecutive_free_block_size*
      .text.esp_mmu_map_reserve_block_with_caps* .text.s_mem_caps_check*
      .text.s_find_available_region* .text.s_reserve_irom_region*
      .text.s_reserve_drom_region*)''','only pre-copy MMU code stays internal')
    if 'ESPCLAW XIP PMP TEXT ALIGN' not in s:
        anchor='    _text_end = ABSOLUTE(.);'
        s=once(s,anchor,'''#ifdef CONFIG_ESP32P4_PSRAM_XIP
    /* ESPCLAW XIP PMP TEXT ALIGN: all real instructions must remain RX. */
    . = ALIGN(128);
#endif
'''+anchor,'PMP instruction boundary')
        anchor='  _image_drom_size = SIZEOF(.flash.appdesc) + SIZEOF(.flash.rodata);'
        s=once(s,anchor,'  _image_drom_size = _rodata_reserved_end - ADDR(.flash.appdesc);','include TLS template in image span')
    pending[sections]=s
    flash=sdk/'components/spi_flash/spi_flash_os_func_app.c';s=flash.read_text()
    if 'ESPCLAW P4 PSRAM XIP' not in s:
        anchor='    _lock_acquire(&s_spi1_flash_mutex);'
        s=once(s,anchor,anchor+'''
#ifdef CONFIG_ESP32P4_PSRAM_XIP
    /* ESPCLAW P4 PSRAM XIP: never use cache-on access before startup proof. */
    extern bool esp32p4_psram_xip_flash_enter(void);
    if (!esp32p4_psram_xip_flash_enter()) {
        _lock_release(&s_spi1_flash_mutex);
        return ESP_ERR_INVALID_STATE;
    }
#endif''','cache-on runtime admission')
    pending[flash]=s
    # Compile the board verification helper only in the selected trial.
    make=root/'arch/risc-v/src/esp32p4/Make.defs';s=make.read_text()
    if '# ESPCLAW P4 PSRAM XIP' not in s:
        s+='\n# ESPCLAW P4 PSRAM XIP\nifeq ($(CONFIG_ESP32P4_PSRAM_XIP),y)\nCHIP_CSRCS += esp32p4_psram_xip.c\nendif\n'
    pending[make]=s
    for path,data in pending.items():
        if path.read_text()!=data:path.write_text(data)
    return {str(p.relative_to(root)):hashlib.sha256(s.encode()).hexdigest() for p,s in pending.items()}
