#!/usr/bin/env python3
"""Simple Boot on ESP32-P4: skip 2nd-stage flash/WDT/APM re-init that causes
HP_SYS_HP_WDT_RESET loops, and sync contest esp_start.c into vela-p4."""
from pathlib import Path

BL = Path(
    "/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/esp-hal-3rdparty/"
    "components/bootloader_support/src/esp32p4/bootloader_esp32p4.c"
)
CONTEST_START = Path(
    "/mnt/c/Users/flash/Desktop/openvela-contest/"
    "contest2026_359_dengfengzaojidecuipidaxuesheng/"
    "chip/esp32p4/common-espressif/esp_start.c"
)
DEST_START = Path(
    "/home/flash/vela-p4/nuttx/arch/risc-v/src/common/espressif/esp_start.c"
)

# Skip analog super-WDT enable, PMP/APM, and flash ID/XMC under NuttX.
# Keep hardware/clock/console/cache so map_rom_segments can talk to flash.
OLD_EARLY = """    bootloader_hardware_init();
    bootloader_ana_reset_config();
    bootloader_super_wdt_auto_feed();

// In RAM_APP, memory will be initialized in `call_start_cpu0`
#if !CONFIG_APP_BUILD_TYPE_RAM
    // protect memory region
    bootloader_init_mem();
    /* check that static RAM is after the stack */
    assert(&_bss_start <= &_bss_end);
    assert(&_data_start <= &_data_end);
    // clear bss section
    bootloader_clear_bss_section();
#endif // !CONFIG_APP_BUILD_TYPE_RAM
"""

NEW_EARLY = """    bootloader_hardware_init();
#if !defined(__NuttX__)
    bootloader_ana_reset_config();
#endif
    bootloader_super_wdt_auto_feed();

// In RAM_APP, memory will be initialized in `call_start_cpu0`
#if !CONFIG_APP_BUILD_TYPE_RAM && !defined(__NuttX__)
    // protect memory region
    bootloader_init_mem();
    /* check that static RAM is after the stack */
    assert(&_bss_start <= &_bss_end);
    assert(&_data_start <= &_data_end);
    // clear bss section
    bootloader_clear_bss_section();
#endif // !CONFIG_APP_BUILD_TYPE_RAM && !__NuttX__
"""

OLD_FLASH = """    // init cache and mmu
    bootloader_init_ext_mem();
    // update flash ID
    bootloader_flash_update_id();
    // Check and run XMC startup flow
    if ((ret = bootloader_flash_xmc_startup()) != ESP_OK) {
        ESP_LOGE(TAG, "failed when running XMC startup flow, reboot!");
        return ret;
    }
#if !defined(__NuttX__)
"""

NEW_FLASH = """    // init cache and mmu
    bootloader_init_ext_mem();
#if !defined(__NuttX__)
    // update flash ID
    bootloader_flash_update_id();
    // Check and run XMC startup flow
    if ((ret = bootloader_flash_xmc_startup()) != ESP_OK) {
        ESP_LOGE(TAG, "failed when running XMC startup flow, reboot!");
        return ret;
    }
"""

OLD_TRACE = """esp_err_t bootloader_init(void)
{
    esp_err_t ret = ESP_OK;

    bootloader_hardware_init();
#if !defined(__NuttX__)
    bootloader_ana_reset_config();
#endif
    bootloader_super_wdt_auto_feed();
"""

NEW_TRACE = """esp_err_t bootloader_init(void)
{
    esp_err_t ret = ESP_OK;

#if defined(__NuttX__)
    ets_printf("B0\\n");
#endif
    bootloader_hardware_init();
#if defined(__NuttX__)
    ets_printf("B1\\n");
#endif
#if !defined(__NuttX__)
    bootloader_ana_reset_config();
#endif
    bootloader_super_wdt_auto_feed();
#if defined(__NuttX__)
    ets_printf("B2\\n");
#endif
"""

OLD_CLK = """    // config clock
    bootloader_clock_configure();
    // initialize console, from now on, we can use esp_log
    bootloader_console_init();
    /* print 2nd bootloader banner */
    bootloader_print_banner();

#if !CONFIG_APP_BUILD_TYPE_RAM
    // init cache and mmu
    bootloader_init_ext_mem();
"""

NEW_CLK = """    // config clock
    bootloader_clock_configure();
#if defined(__NuttX__)
    ets_printf("B3\\n");
#endif
    // initialize console, from now on, we can use esp_log
    bootloader_console_init();
#if defined(__NuttX__)
    ets_printf("B4\\n");
#endif
    /* print 2nd bootloader banner */
    bootloader_print_banner();
#if defined(__NuttX__)
    ets_printf("B5\\n");
#endif

#if !CONFIG_APP_BUILD_TYPE_RAM
    // init cache and mmu
    bootloader_init_ext_mem();
#if defined(__NuttX__)
    ets_printf("B6\\n");
#endif
"""


OLD_HW = """#if defined(__NuttX__)
    ets_printf("B0\\n");
#endif
    bootloader_hardware_init();
#if defined(__NuttX__)
    ets_printf("B1\\n");
#endif
"""

NEW_HW = """#if defined(__NuttX__)
    ets_printf("B0\\n");
#endif
#if !defined(__NuttX__)
    bootloader_hardware_init();
#endif
#if defined(__NuttX__)
    ets_printf("B1\\n");
#endif
"""


OLD_SKIPCLK = """    // config clock
    bootloader_clock_configure();
#if defined(__NuttX__)
    ets_printf("B3\\n");
#endif
"""

NEW_SKIPCLK = """    // config clock
#if defined(__NuttX__)
    ets_printf("B3\\n");
    return ESP_OK;
#endif
    bootloader_clock_configure();
#if defined(__NuttX__)
    ets_printf("B3\\n");
#endif
"""


def patch(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print("already", label, path)
        return
    if old not in text:
        print("skip (no match)", label, path)
        return
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("patched", label, path)


def main() -> None:
    patch(BL, OLD_EARLY, NEW_EARLY, "bootloader-early")
    patch(BL, OLD_FLASH, NEW_FLASH, "bootloader-flash")
    patch(BL, OLD_TRACE, NEW_TRACE, "bootloader-trace")
    patch(BL, OLD_CLK, NEW_CLK, "bootloader-clk")
    patch(BL, OLD_HW, NEW_HW, "bootloader-skip-hw")
    patch(BL, OLD_SKIPCLK, NEW_SKIPCLK, "bootloader-skip-clk")
    text = CONTEST_START.read_text(encoding="utf-8").replace("\r\n", "\n")
    DEST_START.write_text(text, encoding="utf-8")
    print("synced", DEST_START, "N1w", "N1w" in text)


if __name__ == "__main__":
    main()
