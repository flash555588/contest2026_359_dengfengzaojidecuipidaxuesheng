#!/usr/bin/env python3
"""Add dynamically backed module text memory to an explicit prepared P4 tree.

Does not enable XIP or change normal user-heap execute permissions. No flash.
"""
from pathlib import Path

def once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('unsupported prepared source: '+before[:80])
    return text.replace(before, after)

LEGACY_NAMES=[('OV_BROWSER_TEXT_POOL','OV_NATIVE_TEXT_POOL'),
              ('_browser_exec_start','_native_exec_start'),('_browser_exec_end','_native_exec_end'),
              ('Reserved native browser executable pool','Reserved native module executable pool'),
              ('[browser-exec]','[native-exec]')]

def migrate_legacy(paths):
    """Rename an earlier browser-specific patch in place; layout is unchanged."""
    for path in paths:
        if not path.exists():continue
        s=path.read_text();t=s
        for old,new in LEGACY_NAMES:t=t.replace(old,new)
        if t!=s:path.write_text(t)

def apply(nuttx):
    nuttx=Path(nuttx)
    if 'CONFIG_ESP32P4_PSRAM_XIP=y' not in (nuttx/'.config').read_text():
        raise RuntimeError('requires an explicitly prepared XIP baseline; never auto-enables XIP')
    sdk=nuttx/'arch/risc-v/src/esp32p4/esp-hal-3rdparty'
    migrate_legacy([nuttx/'.config',nuttx/'arch/risc-v/src/esp32p4/Kconfig',
                    nuttx/'boards/risc-v/esp32p4/common/scripts/esp32p4_sections.rev3.ld',
                    sdk/'components/esp_hw_support/port/esp32p4/cpu_region_protect.c',
                    nuttx/'arch/risc-v/src/esp32p4/esp32p4_psram_xip.c',
                    nuttx/'arch/risc-v/src/common/espressif/esp_spiflash.c'])
    kconfig=nuttx/'arch/risc-v/src/esp32p4/Kconfig'
    s=kconfig.read_text()
    if 'config OV_NATIVE_TEXT_POOL' not in s:
        s+='''
config OV_NATIVE_TEXT_POOL
    bool "Reserved native module executable pool"
    default n
    depends on ESP32P4_PSRAM_XIP && ESPRESSIF_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
    select ARCH_HAVE_TEXT_HEAP
    select ARCH_USE_TEXT_HEAP
    select MODULE
    select LIBC_ELF
'''
        kconfig.write_text(s)
    linker=nuttx/'boards/risc-v/esp32p4/common/scripts/esp32p4_sections.rev3.ld'
    s=linker.read_text()
    if '_native_exec_start' not in s:
        s=once(s,'    _ext_ram_bss_start = ABSOLUTE(.);', '''    _ext_ram_bss_start = ABSOLUTE(.);
#ifdef CONFIG_OV_NATIVE_TEXT_POOL
    /* Reserve before ordinary BSS; PSRAM allocator excludes this entire span. */
    _native_exec_start = ABSOLUTE(.);
    . += 0x300000;
    _native_exec_end = ABSOLUTE(.);
    ASSERT((_native_exec_start & 0xffff) == 0, "module pool alignment");
#endif''')
        linker.write_text(s)
    s=linker.read_text()
    s=s.replace('ASSERT((_native_exec_start & 0xffff) == 0, "module pool alignment")\n',
                'ASSERT((_native_exec_start & 0xffff) == 0, "module pool alignment");\n')
    linker.write_text(s)
    sdk=nuttx/'arch/risc-v/src/esp32p4/esp-hal-3rdparty'
    protection=sdk/'components/esp_hw_support/port/esp32p4/cpu_region_protect.c'
    s=protection.read_text()
    if 'OV_NATIVE_TEXT_POOL' not in s:
        before='    PMP_ENTRY_SET_CACHED_AND_UNCACHED(14, 19, esp_psram_extram_vaddr_end(), PMP_TOR | RW);'
        s=once(s,before,'''#ifdef CONFIG_OV_NATIVE_TEXT_POOL
    /* Existing XIP code/rodata permissions stay intact. Only the reserved
     * pool gets X; cached ordinary BSS/heap and all uncached heap stay RW.
     * Entry 23 is unused in the prepared v3 table. Its TOR lower bound is
     * entry 22; higher-priority entry 14 covers the native module pool. */
    extern unsigned char _native_exec_end[];
    PMP_RESET_AND_ENTRY_SET(14, (uint32_t)_native_exec_end, PMP_TOR | RWX);
    PMP_RESET_AND_ENTRY_SET(19, CACHE_LL_L2MEM_NON_CACHE_ADDR(esp_psram_extram_vaddr_end()), PMP_TOR | RW);
    PMP_RESET_AND_ENTRY_SET(23, esp_psram_extram_vaddr_end(), PMP_TOR | RW);
#else
'''+before+'\n#endif')
        protection.write_text(s)
    verify=nuttx/'arch/risc-v/src/esp32p4/esp32p4_psram_xip.c'
    s=verify.read_text()
    if 'OV_NATIVE_TEXT_POOL' not in s:
        before='  __asm__ __volatile__("csrr %0, pmpaddr14" : "=r"(heap_end));'
        s=once(s,before,'''#ifdef CONFIG_OV_NATIVE_TEXT_POOL
  uintptr_t module_end, cfg5;
  extern unsigned char _native_exec_end[];
  __asm__ __volatile__("csrr %0, pmpaddr14" : "=r"(module_end));
  __asm__ __volatile__("csrr %0, pmpaddr23" : "=r"(heap_end));
  __asm__ __volatile__("csrr %0, pmpcfg5" : "=r"(cfg5));
  if ((module_end << 2) != (uintptr_t)_native_exec_end ||
      ((cfg5 >> 24) & 0x9f) != 0x8b) xip_fail();
#else
'''+before+'\n#endif')
        s=once(s,'      ((cfg3 >> 16) & 0x9f) != 0x8b || cpu > 1)', '''#ifdef CONFIG_OV_NATIVE_TEXT_POOL
      ((cfg3 >> 16) & 0x9f) != 0x8f || cpu > 1)
#else
      ((cfg3 >> 16) & 0x9f) != 0x8b || cpu > 1)
#endif''')
        before='  atomic_fetch_or_explicit(&g_protected_cpus, 1u << cpu,'
        s=once(s,before,'''#ifdef CONFIG_OV_NATIVE_TEXT_POOL
  esp_rom_printf("[native-exec] cpu=%lu pool_end=%08lx heap_end=%08lx pool=8f heap=8b\\n",
                 (unsigned long)cpu, (unsigned long)(module_end << 2), (unsigned long)(heap_end << 2));
#endif
'''+before)
        verify.write_text(s)
    s=kconfig.read_text().replace('Reserved native module executable pool',
                                 'On-demand native module executable memory')
    kconfig.write_text(s)
    dispatcher = dynamic_pool(nuttx, linker, protection, verify)
    return [kconfig,linker,protection,verify,dispatcher]


def dynamic_pool(nuttx, linker, protection, verify):
    """Migrate the old physical reservation to an unbacked executable alias.

    Cached and uncached ordinary heaps keep RW permissions. SDK mapping still
    uses its original cache protection, on the existing internal-stack worker.
    This prepared board has exactly 32 MiB of physical PSRAM; the upper half of
    the 64 MiB virtual window is not physical RAM and consumes no heap at boot.
    """
    s=linker.read_text()
    old='''    /* Reserve before ordinary BSS; PSRAM allocator excludes this entire span. */
    _native_exec_start = ABSOLUTE(.);
    . += 0x300000;
    _native_exec_end = ABSOLUTE(.);
    ASSERT((_native_exec_start & 0xffff) == 0, "module pool alignment");'''
    new='''    /* Dynamic module alias: no physical PSRAM is reserved here. */
    _native_exec_start = ABSOLUTE(0x4a000000);
    _native_exec_end = ABSOLUTE(0x4c000000);'''
    if old in s:s=once(s,old,new)
    elif new not in s:raise RuntimeError('Unknown native pool reservation')
    linker.write_text(s)
    s=protection.read_text()
    start='''    /* Existing XIP code/rodata permissions stay intact. Only the reserved'''
    end='''    PMP_RESET_AND_ENTRY_SET(23, esp_psram_extram_vaddr_end(), PMP_TOR | RW);'''
    if start in s:
        a=s.index(start);b=s.index(end,a)+len(end)
        s=s[:a]+'''    /* Dynamic module alias: ordinary physical PSRAM stays RW, never RWX.
     * Entry 23 is independent NAPOT, not TOR (P4 v3 DIG-752). */
    PMP_ENTRY_SET_CACHED_AND_UNCACHED(14, 19, esp_psram_extram_vaddr_end(), PMP_TOR | RW);
    PMP_RESET_AND_ENTRY_SET(23, PMPADDR_NAPOT(0x4a000000, 0x4c000000), PMP_NAPOT | RWX);'''+s[b:]
    elif 'Dynamic module alias:' not in s:raise RuntimeError('Unknown native PMP layout')
    protection.write_text(s)
    s=verify.read_text()
    old='''  uintptr_t module_end, cfg5;
  extern unsigned char _native_exec_end[];
  __asm__ __volatile__("csrr %0, pmpaddr14" : "=r"(module_end));
  __asm__ __volatile__("csrr %0, pmpaddr23" : "=r"(heap_end));
  __asm__ __volatile__("csrr %0, pmpcfg5" : "=r"(cfg5));
  if ((module_end << 2) != (uintptr_t)_native_exec_end ||
      ((cfg5 >> 24) & 0x9f) != 0x8b) xip_fail();'''
    new='''  uintptr_t module_alias, cfg5;
  __asm__ __volatile__("csrr %0, pmpaddr14" : "=r"(heap_end));
  __asm__ __volatile__("csrr %0, pmpaddr23" : "=r"(module_alias));
  __asm__ __volatile__("csrr %0, pmpcfg5" : "=r"(cfg5));
  /* Known 32 MiB board only: the alias must never overlap physical heap. */
  if (esp_psram_extram_vaddr_end() != 0x4a000000 ||
      module_alias != ((0x4a000000u >> 2) | 0x003fffffu) ||
      ((cfg5 >> 24) & 0x9f) != 0x9f) xip_fail();'''
    if old in s:s=once(s,old,new)
    elif new not in s:raise RuntimeError('Unknown native PMP verification')
    s=s.replace('((cfg3 >> 16) & 0x9f) != 0x8f || cpu > 1)',
                '((cfg3 >> 16) & 0x9f) != 0x8b || cpu > 1)')
    s=s.replace('''  esp_rom_printf("[native-exec] cpu=%lu pool_end=%08lx heap_end=%08lx pool=8f heap=8b\\n",
                 (unsigned long)cpu, (unsigned long)(module_end << 2), (unsigned long)(heap_end << 2));''',
                '''  esp_rom_printf("[native-exec] cpu=%lu reserved=0 alias=4a000000..4c000000 heap_end=%08lx heap=8b\\n",
                 (unsigned long)cpu, (unsigned long)(heap_end << 2));''')
    verify.write_text(s)
    dispatcher=nuttx/'arch/risc-v/src/common/espressif/esp_spiflash.c'
    s=dispatcher.read_text()
    if 'esp32p4_internal_call' not in s:
        s=once(s,'static int spiflash_stack_worker(int argc, char **argv)',
'''#ifdef CONFIG_OV_NATIVE_TEXT_POOL
struct internal_request { int (*call)(void *); void *argument; };
#endif

static int spiflash_stack_worker(int argc, char **argv)''')
        anchor='''    else if (g_flash_job.operation == 0)'''
        s=once(s,anchor,'''#ifdef CONFIG_OV_NATIVE_TEXT_POOL
    else if (g_flash_job.operation == 3) {
      struct internal_request *request = g_flash_job.buffer;
      int (*call)(void *) = request->call;
      void *argument = request->argument;
      g_flash_job.result = call(argument);
    }
#endif
'''+anchor)
        anchor='int esp_spiflash_read(uint32_t address, void *buffer, uint32_t length)'
        s=once(s,anchor,'''#ifdef CONFIG_OV_NATIVE_TEXT_POOL
/* Synchronous: caller arguments remain live until the internal-stack callback
 * completes. The existing mutex serializes mapping and Flash dispatch jobs. */
int esp32p4_internal_call(int (*call)(void *), void *argument)
{
  if (!call) return -EINVAL;
  struct internal_request request = {call, argument};
  return spiflash_dispatch(3, 0, &request, 0);
}
#endif

'''+anchor)
        dispatcher.write_text(s)
    return dispatcher
