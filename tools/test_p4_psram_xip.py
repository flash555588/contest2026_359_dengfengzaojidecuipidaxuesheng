#!/usr/bin/env python3
"""Exercise the production XIP startup verifier and real SDK SPI1 OS branch."""
import argparse
from pathlib import Path
import shutil,subprocess
from test_display_flash import function

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--nuttx',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parent.parent
inc=out/'include'
headers={
 'nuttx/config.h':'#define CONFIG_SPIRAM_FETCH_INSTRUCTIONS 1\n#define CONFIG_SPIRAM_RODATA 1\n#define CONFIG_SPIRAM_FLASH_LOAD_TO_PSRAM 1\n#define CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 1\n#define CONFIG_ESP32P4_PSRAM_XIP 1\n#define CONFIG_MMU_PAGE_SIZE 0x10000\n',
 'esp_attr.h':'#define IRAM_ATTR\n#define DRAM_ATTR\n',
 'esp_rom_sys.h':'int esp_rom_printf(const char *, ...);\n',
 'esp_private/esp_psram_extram.h':'#include <stdint.h>\nuintptr_t esp_psram_extram_vaddr_start(void);\nuintptr_t esp_psram_extram_vaddr_end(void);\n',
 'hal/mmu_hal.h':'#include <stdint.h>\n#include <stdbool.h>\ntypedef enum { MMU_TARGET_FLASH0=1, MMU_TARGET_PSRAM0=2 } mmu_target_t;\nbool mmu_hal_vaddr_to_paddr(unsigned,uint32_t,uint32_t *,mmu_target_t *);\n',
 'hal/mmu_ll.h':'#define MMU_LL_PSRAM_MMU_ID 1\n#define MMU_LL_FLASH_MMU_ID 0\n',
 'soc/ext_mem_defs.h':'#define SOC_DRAM_PSRAM_ADDRESS_LOW 0x48000000u\n#define SOC_DRAM_PSRAM_ADDRESS_HIGH 0x4c000000u\n#define SOC_DRAM_FLASH_PSRAM_OFFSET 0x08000000u\n',
}
for name,data in headers.items():
 target=inc/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(data)
sdk=a.nuttx/'arch/risc-v/src/esp32p4/esp-hal-3rdparty'
os_source=(sdk/'components/spi_flash/spi_flash_os_func_app.c').read_text()
production=repo/'app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/esp32p4/esp32p4_psram_xip.c'
os_part='''
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 259
typedef int esp_err_t;
typedef struct { uint32_t start_flags; } app_func_arg_t;
static int s_spi1_flash_mutex;
static unsigned acquired, released, cache_off;
void _lock_acquire(int *p){assert(!*p);*p=1;}
void _lock_release(int *p){assert(*p);*p=0;}
void cache_disable(void *p){(void)p;cache_off++;}
void cache_enable(void *p){(void)p;cache_off++;}
void on_spi_acquired(app_func_arg_t *p){(void)p;acquired++;}
void on_spi_released(app_func_arg_t *p){(void)p;released++;}
'''
os_part+=next(l for l in os_source.splitlines() if l.startswith('#define SPI_FLASH_CACHE_NO_DISABLE '))+'\n'
os_part+=function(os_source,'spi1_start','static IRAM_ATTR esp_err_t ')+'\n'
os_part+=function(os_source,'spi1_end','static IRAM_ATTR esp_err_t ')+'\n'
code='''#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
'''+f'#include "{production}"\n'+os_part+'''
__asm__(".global _instruction_reserved_start\\n.set _instruction_reserved_start,0x48000000\\n"
        ".global _instruction_reserved_end\\n.set _instruction_reserved_end,0x48010020\\n"
        ".global _rodata_reserved_start\\n.set _rodata_reserved_start,0x48020020\\n"
        ".global _rodata_reserved_end\\n.set _rodata_reserved_end,0x48030080\\n"
        ".global _ext_ram_bss_start\\n.set _ext_ram_bss_start,0x48040000\\n"
        ".global _ext_ram_bss_end\\n.set _ext_ram_bss_end,0x48048000\\n");
static unsigned bad;
int esp_rom_printf(const char *fmt, ...){(void)fmt;return 0;}
uintptr_t esp_psram_extram_vaddr_start(void){return bad==3?0x48047000:0x48050000;}
uintptr_t esp_psram_extram_vaddr_end(void){return 0x4a000000;}
void image_process_get_flash_segments_info(uint32_t *d,uint32_t *i){*d=0x30020;*i=0x10020;}
bool mmu_hal_vaddr_to_paddr(unsigned id,uint32_t v,uint32_t *p,mmu_target_t *t){
 if(id==1){
  *t=bad==2?MMU_TARGET_FLASH0:MMU_TARGET_PSRAM0;
  *p=v<0x48020000?v-0x48000000+0x20000:v-0x48020000;
 }else{*t=MMU_TARGET_FLASH0;*p=v<0x40020000?v-0x40000000+0x10020:v-0x40020020+0x30020;}
 return true;
}
int main(void){
 assert(mmap((void*)0x48000000,0x60000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)0x48000000);
 assert(mmap((void*)0x40000000,0x60000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)0x40000000);
 for(unsigned i=0;i<0x60000;i++)((unsigned char*)0x48000000)[i]=(i*17u+i/91u)&255;
 memcpy((void*)0x40000000,(void*)0x48000000,0x60000);
 app_func_arg_t ctx={0};
 assert(spi1_start(&ctx,7)==ESP_ERR_INVALID_STATE&&!s_spi1_flash_mutex);
 assert(acquired==0&&cache_off==0);
 esp32p4_psram_xip_verify();assert(g_xip_ready);
 assert(spi1_start(&ctx,7)==ESP_ERR_INVALID_STATE&&!s_spi1_flash_mutex);
 atomic_store(&g_protected_cpus,1); /* CSR proof is performed on the board. */
 for(unsigned i=0;i<100;i++){
  assert(spi1_start(&ctx,7)==0&&s_spi1_flash_mutex&&ctx.start_flags==7);
  assert(spi1_end(&ctx)==0&&!s_spi1_flash_mutex);
 }
 assert(acquired==100&&released==100&&cache_off==0);
 for(unsigned fault=1;fault<=3;fault++){
  pid_t child=fork();assert(child>=0);
  if(!child){bad=fault;if(fault==1)((unsigned char*)0x48000000)[713]^=1;esp32p4_psram_xip_verify();_exit(0);}
  int status;assert(waitpid(child,&status,0)==child);
  assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 }
 puts("P4_PSRAM_XIP_OK: real verifier good/corrupt/MMU-target/heap-overlap; SDK SPI1 ready gate and 100 mutex pairs, zero cache-off");
}
'''
(out/'test.c').write_text(code)
subprocess.run(['gcc','-std=gnu11','-O1','-g','-fno-pie','-no-pie','-fsanitize=undefined','-I'+str(inc),str(out/'test.c'),'-o',str(out/'test')],check=True)
subprocess.run([str(out/'test')],check=True)
