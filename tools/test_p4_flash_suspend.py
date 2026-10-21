#!/usr/bin/env python3
"""Check actual SDK config translation, GD capability and cache-on OS branch."""
import argparse
from pathlib import Path
import shutil
import subprocess
from prepare_p4_flash_suspend import apply
from test_display_flash import function

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--nuttx',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
relative=Path('arch/risc-v/src/esp32p4/esp-hal-3rdparty')
sdk=a.nuttx/relative
fixture=out/'prepared'
paths=['nuttx/esp32p4/include/sdkconfig.h','components/spi_flash/esp_flash_spi_init.c']
for name in paths:
 dest=fixture/relative/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(sdk/name,dest)
apply(fixture)
first={name:(fixture/relative/name).read_bytes() for name in paths}
apply(fixture)
assert all((fixture/relative/name).read_bytes()==data for name,data in first.items())
stub=out/'include/nuttx';stub.mkdir(parents=True,exist_ok=True)
header=fixture/relative/paths[0]
for enabled in [False,True]:
 (stub/'config.h').write_text('#define CONFIG_ESP32P4_REV_MIN_301 1\n#define CONFIG_ESP32P4_REV_MIN_FULL 301\n#define CONFIG_ESPRESSIF_FLASH_FREQ_80M 1\n'+('#define CONFIG_ESP32P4_FLASH_AUTO_SUSPEND 1\n' if enabled else ''))
 defs=subprocess.check_output(['gcc','-dM','-E','-I'+str(out/'include'),str(header)],text=True)
 assert ('#define CONFIG_SPI_FLASH_AUTO_SUSPEND 1' in defs)==enabled
 assert ('#define CONFIG_SPI_FLASH_SUPPORT_GD_CHIP 1' in defs)==enabled
 assert 'CONFIG_SPI_FLASH_SHARE_SPI1_BUS' not in defs
 assert 'CONFIG_SPI_FLASH_DISABLE_SCHEDULER_IN_SUSPEND' not in defs
 assert '#define CONFIG_SPI_FLASH_SUSPEND_TSUS_VAL_US 50' in defs
 if enabled:assert '#define CONFIG_SPI_FLASH_SUSPEND_TRS_VAL_US 50' in defs
chip=(sdk/'components/spi_flash/spi_flash_chip_gd.c').read_text()
os_source=(sdk/'components/spi_flash/spi_flash_os_func_app.c').read_text()
production=function(chip,'spi_flash_chip_gd_get_caps','spi_flash_caps_t ')+'\n'
production+=function(os_source,'spi1_start','static IRAM_ATTR esp_err_t ')+'\n'
production+=function(os_source,'spi1_end','static IRAM_ATTR esp_err_t ')+'\n'
init_source=(fixture/relative/paths[1]).read_text().replace(
 'ESP_SYSTEM_INIT_FN(init_flash, CORE, BIT(0), 130)','esp_err_t test_init_flash(void)')
production+=function(init_source,'test_init_flash','esp_err_t ')+'\n'
before='''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define IRAM_ATTR
#define CONFIG_SPI_FLASH_AUTO_SUSPEND 1
#define CONFIG_ESP32P4_FLASH_AUTO_SUSPEND 1
#define SPI_FLASH_CHIP_CAP_32MB_SUPPORT 1
#define SPI_FLASH_CHIP_CAP_SUSPEND 2
#define SPI_FLASH_CHIP_CAP_UNIQUE_ID 4
#define ESP_OK 0
typedef int esp_err_t;
typedef uint32_t spi_flash_caps_t;
typedef struct {uint32_t chip_id;} esp_flash_t;
typedef struct {uint32_t start_flags;} app_func_arg_t;
static int s_spi1_flash_mutex;
static unsigned acquired,released,cache_off;
static int app_error,chip_error;
static unsigned chip_calls,callback_calls;
int esp_flash_app_init(void){return app_error;}
int esp_flash_init_default_chip(void){chip_calls++;return chip_error;}
bool spi_flash_cache_enabled(void){return true;}
void esp_log_util_set_cache_enabled_cb(bool (*cb)(void)){assert(cb());callback_calls++;}
void _lock_acquire(int *p){assert(!*p);*p=1;}
void _lock_release(int *p){assert(*p);*p=0;}
void cache_disable(void *p){(void)p;cache_off++;}
void cache_enable(void *p){(void)p;cache_off++;}
void on_spi_acquired(app_func_arg_t *p){(void)p;acquired++;}
void on_spi_released(app_func_arg_t *p){(void)p;released++;}
'''
before+=next(line for line in os_source.splitlines() if line.startswith('#define SPI_FLASH_CACHE_NO_DISABLE '))+'\n'
after='''int main(void){
 esp_flash_t gd={0xc84018},other={0xc84019};
 assert(spi_flash_chip_gd_get_caps(&gd)&SPI_FLASH_CHIP_CAP_SUSPEND);
 assert(!(spi_flash_chip_gd_get_caps(&other)&SPI_FLASH_CHIP_CAP_SUSPEND));
 app_func_arg_t ctx={0};
 for(unsigned i=0;i<100;i++){
  assert(spi1_start(&ctx,7)==0&&s_spi1_flash_mutex&&ctx.start_flags==7);
  assert(spi1_end(&ctx)==0&&!s_spi1_flash_mutex);
 }
 assert(acquired==100&&released==100&&cache_off==0);
 app_error=17;assert(test_init_flash()==17&&chip_calls==0&&callback_calls==0);
 app_error=0;chip_error=29;assert(test_init_flash()==29&&chip_calls==1&&callback_calls==0);
 chip_error=0;assert(test_init_flash()==0&&chip_calls==2&&callback_calls==1);
 puts("P4_FLASH_SUSPEND_OK: actual GD C84018 capability, unsupported ID, actual SPI1 mutex start/end with zero cache disable, SDK config on/off and timing");
 return 0;
}
'''
(out/'suspend.c').write_text(before+production+after)
exe=out/'suspend'
subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-O1','-g',
 '-fsanitize=address,undefined','-fno-pie','-no-pie',str(out/'suspend.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
