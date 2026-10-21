"""Wire supported P4/GD Flash auto-suspend into the prepared NuttX SDK."""
import hashlib
from pathlib import Path
from protect_p4_display_flash import once


def apply(nuttx):
    root=Path(nuttx)/'arch/risc-v/src/esp32p4/esp-hal-3rdparty'
    config=root/'nuttx/esp32p4/include/sdkconfig.h'
    init=root/'components/spi_flash/esp_flash_spi_init.c'
    pending={};receipts={}
    for path in [config,init]:
        source=path.read_text()
        if 'ESPCLAW P4 FLASH AUTO SUSPEND' not in source:
            if path==config:
                anchor='#define CONFIG_SPI_FLASH_PLACE_FUNCTIONS_IN_IRAM 1'
                source=once(source,anchor,'''/* ESPCLAW P4 FLASH AUTO SUSPEND */
#if defined(CONFIG_ESP32P4_FLASH_AUTO_SUSPEND)
#define CONFIG_SPI_FLASH_AUTO_SUSPEND 1
#define CONFIG_SPI_FLASH_SUPPORT_GD_CHIP 1
#ifndef CONFIG_SPI_FLASH_SUSPEND_TRS_VAL_US
#define CONFIG_SPI_FLASH_SUSPEND_TRS_VAL_US 50
#endif
#if CONFIG_SPI_FLASH_SHARE_SPI1_BUS || CONFIG_SPI_FLASH_DISABLE_SCHEDULER_IN_SUSPEND
#error "P4 auto suspend requires the cache-on NuttX mutex path"
#endif
#endif
'''+anchor,'P4 auto-suspend config')
            else:
                source=once(source,'#include "sdkconfig.h"',
                    '#include "sdkconfig.h"\n/* ESPCLAW P4 FLASH AUTO SUSPEND */\n#ifdef CONFIG_ESP32P4_FLASH_AUTO_SUSPEND\n#include <stdio.h>\n#endif','auto-suspend diagnostic include')
                anchor='''#ifdef CONFIG_SPI_FLASH_AUTO_SUSPEND
    err = esp_flash_suspend_cmd_init(&default_chip);'''
                source=once(source,anchor,'''#ifdef CONFIG_SPI_FLASH_AUTO_SUSPEND
#ifdef CONFIG_ESP32P4_FLASH_AUTO_SUSPEND
    /* Only the measured GD C84018 board is qualified by this integration. */
    if (default_chip.chip_id != 0xC84018) {
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    err = esp_flash_suspend_cmd_init(&default_chip);''','measured chip gate')
                source+='''
#ifdef CONFIG_ESP32P4_FLASH_AUTO_SUSPEND
void esp32p4_flash_auto_diagnostics(void)
{
    esp_flash_t *chip = esp_flash_default_chip;
    unsigned caps = chip && chip->chip_drv && chip->chip_drv->get_chip_caps ?
                    chip->chip_drv->get_chip_caps(chip) : 0;
    printf("[flash-auto] configured=1 id=%06lx driver=%s capable=%u tsus_us=%u trs_us=%u\\n",
           (unsigned long)(chip ? chip->chip_id : 0),
           chip && chip->chip_drv ? chip->chip_drv->name : "none",
           !!(caps & SPI_FLASH_CHIP_CAP_SUSPEND),
           CONFIG_SPI_FLASH_SUSPEND_TSUS_VAL_US,
           CONFIG_SPI_FLASH_SUSPEND_TRS_VAL_US);
}
#endif
'''
        if path==config and '#ifndef CONFIG_SPI_FLASH_SUSPEND_TRS_VAL_US' not in source:
            source=once(source,'#define CONFIG_SPI_FLASH_SUPPORT_GD_CHIP 1\n',
                '#define CONFIG_SPI_FLASH_SUPPORT_GD_CHIP 1\n#ifndef CONFIG_SPI_FLASH_SUSPEND_TRS_VAL_US\n#define CONFIG_SPI_FLASH_SUSPEND_TRS_VAL_US 50\n#endif\n#if CONFIG_SPI_FLASH_SHARE_SPI1_BUS || CONFIG_SPI_FLASH_DISABLE_SCHEDULER_IN_SUSPEND\n#error "P4 auto suspend requires the cache-on NuttX mutex path"\n#endif\n','MIN301 TRS and mutex gate')
        if path==init and 'ESPCLAW FLASH STARTUP CHECK' not in source:
            old='''    esp_flash_app_init();
    esp_err_t flash_ret = esp_flash_init_default_chip();
    assert(flash_ret == ESP_OK);
    (void)flash_ret;'''
            new='''#ifdef CONFIG_ESP32P4_FLASH_AUTO_SUSPEND
    /* ESPCLAW FLASH STARTUP CHECK: the caller aborts on nonzero result.
     * Do not depend on assert(), which is absent from release builds.
     */
    esp_err_t flash_ret = esp_flash_app_init();
    if (flash_ret != ESP_OK) return flash_ret;
    flash_ret = esp_flash_init_default_chip();
    if (flash_ret != ESP_OK) return flash_ret;
#else
'''+old+'''
#endif'''
            source=once(source,old,new,'non-assert startup failure propagation')
        required='CONFIG_SPI_FLASH_SUPPORT_GD_CHIP 1' if path==config else 'default_chip.chip_id != 0xC84018'
        if required not in source:raise RuntimeError('Incomplete P4 auto-suspend integration')
        pending[path]=source
        receipts[str(path.relative_to(root))]=hashlib.sha256(source.encode()).hexdigest()
    for path,source in pending.items():
        if path.read_text()!=source:path.write_text(source)
    return receipts
