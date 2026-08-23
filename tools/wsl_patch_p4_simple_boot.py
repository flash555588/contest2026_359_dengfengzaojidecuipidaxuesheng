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
    # ESP-IDF component sources include their generated sdkconfig.h, which
    # does not expose NuttX-only Kconfig symbols.  Pull in NuttX config first
    # so the v1.x/v3.x boot paths below are selected by the board defconfig
    # on every clean build, not by stale object files.
    bltext = BL.read_text(encoding="utf-8")
    config_include = "#if defined(__NuttX__)\n#include <nuttx/config.h>\n#endif\n"
    if config_include not in bltext:
        bltext = bltext.replace(
            "#include <stdint.h>\n",
            "#include <stdint.h>\n" + config_include,
            1,
        )
        BL.write_text(bltext, encoding="utf-8")
        print("included NuttX config in ESP32-P4 bootloader", BL)

    patch(BL, OLD_EARLY, NEW_EARLY, "bootloader-early")
    patch(BL, OLD_FLASH, NEW_FLASH, "bootloader-flash")
    patch(BL, OLD_TRACE, NEW_TRACE, "bootloader-trace")
    patch(BL, OLD_CLK, NEW_CLK, "bootloader-clk")
    patch(BL, OLD_HW, NEW_HW, "bootloader-skip-hw")
    patch(BL, OLD_SKIPCLK, NEW_SKIPCLK, "bootloader-skip-clk")

    # A NuttX Simple-Boot image is entered after the ROM has already set up
    # the executable mapping and clocks.  Re-running the IDF second-stage
    # bootloader path is unsafe on every revision: v1.x loses the live MSPI
    # mapping, while v3.2 stalls in the analog BIAS REGI2C write.  Keep the
    # ROM state for both paths; revision-specific linker scripts and later
    # NuttX PSRAM initialization remain selected by the board defconfig.
    bltext = BL.read_text(encoding="utf-8")
    bltext = bltext.replace(
        "#if !defined(__NuttX__) || !CONFIG_ESP32P4_SELECTS_REV_LESS_V3\n",
        "#if !defined(__NuttX__)\n",
    )
    bltext = bltext.replace(
        "#if !CONFIG_APP_BUILD_TYPE_RAM && "
        "(!defined(__NuttX__) || !CONFIG_ESP32P4_SELECTS_REV_LESS_V3)\n",
        "#if !CONFIG_APP_BUILD_TYPE_RAM && !defined(__NuttX__)\n",
    )
    bltext = bltext.replace(
        '#if defined(__NuttX__) && CONFIG_ESP32P4_SELECTS_REV_LESS_V3\n'
        '    ets_printf("B3\\n");\n    return ESP_OK;\n#endif',
        '#if defined(__NuttX__)\n'
        '    ets_printf("B3\\n");\n    return ESP_OK;\n#endif',
    )

    old_hw_body = """static inline void bootloader_hardware_init(void)
{
    _regi2c_ctrl_ll_master_enable_clock(true); // keep ana i2c mst clock always enabled in bootloader
    regi2c_ctrl_ll_master_configure_clock();

    unsigned chip_version = efuse_hal_chip_revision();
    if (!ESP_CHIP_REV_ABOVE(chip_version, 1)) {
        // On ESP32P4 ECO0, the default (power on reset) CPLL and SPLL frequencies are very high, lower them to avoid bias may not be enough in bootloader
        // And we are fixing SPLL to be 480MHz after app is up
        REGI2C_WRITE_MASK(I2C_CPLL, I2C_CPLL_OC_DIV_7_0, 6); // lower default cpu_pll freq to 400M
        REGI2C_WRITE_MASK(I2C_SYSPLL, I2C_SYSPLL_OC_DIV_7_0, 8); // lower default sys_pll freq to 480M
        esp_rom_delay_us(100);
    }
    REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1, 10);
    REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1_PVT, 10);

#if !CONFIG_APP_BUILD_TYPE_PURE_RAM_APP
    // IDF-10019 TODO: This is temporarily for ESP32P4-ECO0, please remove it when eco0 is not widly used.
    if (likely(ESP_CHIP_REV_ABOVE(chip_version, 1))) {
        bootloader_init_mspi_clock();
    }
#endif
}
"""
    new_hw_body = """static inline void bootloader_hardware_init(void)
{
#if defined(__NuttX__)
    ets_printf("H0\\n");
#endif
    _regi2c_ctrl_ll_master_enable_clock(true); // keep ana i2c mst clock always enabled in bootloader
    regi2c_ctrl_ll_master_configure_clock();
#if defined(__NuttX__)
    ets_printf("H1\\n");
#endif

    unsigned chip_version = efuse_hal_chip_revision();
#if defined(__NuttX__)
    ets_printf("H2 rev=%u\\n", chip_version);
#endif
    if (!ESP_CHIP_REV_ABOVE(chip_version, 1)) {
        // On ESP32P4 ECO0, the default (power on reset) CPLL and SPLL frequencies are very high, lower them to avoid bias may not be enough in bootloader
        // And we are fixing SPLL to be 480MHz after app is up
        REGI2C_WRITE_MASK(I2C_CPLL, I2C_CPLL_OC_DIV_7_0, 6); // lower default cpu_pll freq to 400M
        REGI2C_WRITE_MASK(I2C_SYSPLL, I2C_SYSPLL_OC_DIV_7_0, 8); // lower default sys_pll freq to 480M
        esp_rom_delay_us(100);
    }
#if defined(__NuttX__)
    ets_printf("H3\\n");
#endif
    REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1, 10);
    REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1_PVT, 10);
#if defined(__NuttX__)
    ets_printf("H4\\n");
#endif

#if !CONFIG_APP_BUILD_TYPE_PURE_RAM_APP && !defined(__NuttX__)
    // IDF-10019 TODO: This is temporarily for ESP32P4-ECO0, please remove it when eco0 is not widly used.
    if (likely(ESP_CHIP_REV_ABOVE(chip_version, 1))) {
        bootloader_init_mspi_clock();
    }
#endif
#if defined(__NuttX__)
    // The ROM already configured MSPI to read this Simple Boot image.  Do
    // not switch its source while the application image is being entered.
    ets_printf("H5\\n");
#endif
}
"""
    if old_hw_body in bltext:
        bltext = bltext.replace(old_hw_body, new_hw_body, 1)
        print("patched NuttX P4 hardware init to preserve ROM MSPI clock", BL)
    elif new_hw_body not in bltext:
        raise SystemExit("could not patch P4 bootloader_hardware_init body")

    BL.write_text(bltext, encoding="utf-8")
    print("preserved ROM Simple Boot state on all chip revisions", BL)

    text = CONTEST_START.read_text(encoding="utf-8").replace("\r\n", "\n")
    DEST_START.write_text(text, encoding="utf-8")
    print("synced", DEST_START, "N1w", "N1w" in text)


if __name__ == "__main__":
    main()
