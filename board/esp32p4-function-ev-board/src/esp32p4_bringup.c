/****************************************************************************
 * boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_bringup.c
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
#include <nuttx/arch.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <nuttx/i2c/i2c_master.h>
#include "esp_i2c.h"
#include <nuttx/input/gt9xx.h>
#include "esp_ldo.h"

/* Enable Simple-Boot PSRAM bring-up (D1) */
#define PSRAM_SIMPLE_BOOT 1
/* Enable Simple-Boot GT911 touch (D4) */
#define GT911_SIMPLE_BOOT 1

#include <debug.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <nuttx/fs/fs.h>

#include "esp_board_ledc.h"
#include "esp_board_spiflash.h"
#include "esp_board_i2c.h"
#include "esp_board_bmp180.h"

#include "espressif/esp_start.h"

#ifdef CONFIG_WATCHDOG
#  include "espressif/esp_wdt.h"
#endif

#ifdef CONFIG_TIMER
#  include "espressif/esp_gptimer.h"
#endif

#ifdef CONFIG_ONESHOT
#  include "espressif/esp_oneshot.h"
#endif

#ifdef CONFIG_RTC_DRIVER
#  include "espressif/esp_rtc.h"
#endif

#ifdef CONFIG_DEV_GPIO
#  include "espressif/esp_gpio.h"
#endif

#ifdef CONFIG_INPUT_BUTTONS
#  include <nuttx/input/buttons.h>
#endif

#ifdef CONFIG_ESPRESSIF_EFUSE
#  include "espressif/esp_efuse.h"
#endif

#ifdef CONFIG_ESP_RMT
#  include "esp_board_rmt.h"
#endif

#ifdef CONFIG_ESPRESSIF_I2S
#  include "esp_board_i2s.h"
#endif

#ifdef CONFIG_ESPRESSIF_SPI
#  include "espressif/esp_spi.h"
#  include "esp_board_spidev.h"
#  ifdef CONFIG_ESPRESSIF_SPI_BITBANG
#    include "espressif/esp_spi_bitbang.h"
#  endif
#endif

#ifdef CONFIG_SPI_SLAVE_DRIVER
#  include "espressif/esp_spi.h"
#  include "esp_board_spislavedev.h"
#endif

#ifdef CONFIG_ESPRESSIF_TEMP
#  include "espressif/esp_temperature_sensor.h"
#endif

#ifdef CONFIG_ESP_MCPWM
#  include "esp_board_mcpwm.h"
#endif

#ifdef CONFIG_ESP_PCNT
#  include "espressif/esp_pcnt.h"
#  include "esp_board_pcnt.h"
#endif

#ifdef CONFIG_ESPRESSIF_ADC
#  include "esp_board_adc.h"
#endif

#ifdef CONFIG_PM
#  include "espressif/esp_pm.h"
#endif

#ifdef CONFIG_SYSTEM_NXDIAG_ESPRESSIF_CHIP_WO_TOOL
#  include "espressif/esp_nxdiag.h"
#endif

#ifdef CONFIG_ESP_SDM
#  include "espressif/esp_sdm.h"
#endif

#ifdef CONFIG_COMP
#  include "espressif/esp_ana_cmpr.h"
#endif

#ifdef CONFIG_ESPRESSIF_USE_LP_CORE
#  include "espressif/esp_ulp.h"
#  ifdef CONFIG_ESPRESSIF_ULP_USE_TEST_BIN
#    include "ulp/ulp_code.h"
#  endif
#  ifdef CONFIG_ESPRESSIF_LP_MAILBOX
#    include "espressif/esp_lp_mailbox.h"
#  endif
#endif

#include "esp32p4-function-ev-board.h"

#if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
extern int ets_printf(const char *fmt, ...);
#  define bringup_progress(s) ets_printf(s)
#else
#  define bringup_progress(s)
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: esp_bringup
 *
 * Description:
 *   Perform architecture-specific initialization.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; A negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

/* GT911 board callbacks: INT line is NC on this adapter, the driver runs
 * in polling mode (CONFIG_INPUT_GT9XX_POLL), so these are no-ops. */
static int  gt911_irq_attach(const struct gt9xx_board_s *state,
                             xcpt_t isr, FAR void *arg)
{
  return OK;
}
static void gt911_irq_enable(const struct gt9xx_board_s *state, bool enable)
{
}
static int  gt911_set_power(const struct gt9xx_board_s *state, bool on)
{
  return OK;
}

static const struct gt9xx_board_s g_touch =
  { gt911_irq_attach, gt911_irq_enable, gt911_set_power };

/* The generic gt9xx driver registers a character device without probing the
 * bus.  Probe explicitly here so "registered" means that the panel actually
 * acknowledged its product-ID register. */

static int gt911_probe(FAR struct i2c_master_s *i2c, uint8_t addr,
                       FAR uint8_t *id)
{
  uint8_t reg[2] = { 0x81, 0x40 };
  struct i2c_msg_s msgv[2] =
    {
      {
        .frequency = CONFIG_INPUT_GT9XX_I2C_FREQUENCY,
        .addr = addr,
        .flags = 0,
        .buffer = reg,
        .length = sizeof(reg),
      },
      {
        .frequency = CONFIG_INPUT_GT9XX_I2C_FREQUENCY,
        .addr = addr,
        .flags = I2C_M_READ,
        .buffer = id,
        .length = 4,
      },
    };

  return I2C_TRANSFER(i2c, msgv, 2);
}

int esp_bringup(void)
{
  bringup_progress("A2\n");
#ifdef PSRAM_SIMPLE_BOOT
  /* Simple Boot PSRAM bring-up. Runs here (console alive) so every step
   * is visible; the early-start attempt in esp_start.c was removed because
   * its logs fell into the MSPI clock-switch UART dead window.
   * Local mirror of struct esp_ldo_config_t (arch .../common/espressif/esp_ldo.h).
   */
  extern int   esp_ldo_channel_acquire(struct esp_ldo_config_t *config);
  extern int   esp_psram_chip_init(void);
  extern int   esp_psram_init(void);
  extern bool  esp_psram_is_initialized(void);
  extern size_t esp_psram_get_size(void);

  struct esp_ldo_config_t psram_ldo;
  int pret;

  psram_ldo.chan_id    = 2;      /* VDD_PSRAM domain, per schematic */
  psram_ldo.voltage_mv = 1800;
  psram_ldo.handler    = NULL;

  if (!esp_psram_is_initialized())
    {
      pret = esp_ldo_channel_acquire(&psram_ldo);
      printf("PSRAM: late ldo -> %d\n", pret);

      if (pret == OK)
        {
          pret = esp_psram_chip_init();
          if (pret == 0)
            {
              pret = esp_psram_init();
            }
        }
    }

  printf("PSRAM: size=%u initialized=%d\n",
         (unsigned)esp_psram_get_size(),
         esp_psram_is_initialized() ? 1 : 0);
  bringup_progress("A3\n");

#ifdef CONFIG_ESPRESSIF_MIPI_DSI
  int dret;
  {
    extern int esp32p4_display_init(void);
    bringup_progress("A5\n");
    dret = esp32p4_display_init();
    printf("DISP: display_init -> %d\n", dret);
    bringup_progress("A6\n");
  }
#endif

#ifdef GT911_SIMPLE_BOOT
  {
    extern struct i2c_master_s *esp_i2cbus_initialize(int port);

    /* The official ESP32-P4 Function EV Board BSP routes touch to HP I2C1;
     * GPIO7/GPIO8 are shared module pins, not an I2C0-only mapping.
     * Initialize touch only after the MIPI-DSI host is up: the I2C1 poll
     * timer and DSI host share boot-time clock resources, and touching the
     * bus before display init makes the first panel DCS write time out. */

    FAR struct i2c_master_s *i2c = esp_i2cbus_initialize(ESPRESSIF_I2C1);
    int tret = -1;

    if (i2c != NULL)
      {
        uint8_t id[4];
        uint8_t addr = 0x5d;

        memset(id, 0, sizeof(id));
        tret = gt911_probe(i2c, addr, id);
        if (tret < 0)
          {
            addr = 0x14;
            tret = gt911_probe(i2c, addr, id);
          }

        printf("TOUCH: probe addr=0x%02x -> %d id=%c%c%c%c\n",
               addr, tret, isprint(id[0]) ? id[0] : '?',
               isprint(id[1]) ? id[1] : '?',
               isprint(id[2]) ? id[2] : '?',
               isprint(id[3]) ? id[3] : '?');

        if (tret == 0)
          {
            tret = gt9xx_register("/dev/input0", i2c, addr, &g_touch);
          }
        if (tret < 0)
          {
            printf("TOUCH: gt911 not registered: %d\n", tret);
          }
      }

    printf("TOUCH: gt911 /dev/input0 -> %d\n", tret);
    bringup_progress("A4\n");
  }
#endif

#endif

  int ret = OK;

#ifdef CONFIG_FS_PROCFS
  /* Mount the procfs file system */

  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      _err("Failed to mount procfs at /proc: %d\n", ret);
    }
#endif

#ifdef CONFIG_FS_TMPFS
  /* Mount the tmpfs file system */

  ret = nx_mount(NULL, CONFIG_LIBC_TMPDIR, "tmpfs", 0, NULL);
  if (ret < 0)
    {
      _err("Failed to mount tmpfs at %s: %d\n", CONFIG_LIBC_TMPDIR, ret);
    }
#endif

#if defined(CONFIG_ESPRESSIF_EFUSE)
  ret = esp_efuse_initialize("/dev/efuse");
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to init EFUSE: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_MWDT0
  ret = esp_wdt_initialize("/dev/watchdog0", ESP_WDT_MWDT0);
  if (ret < 0)
    {
      _err("Failed to initialize WDT: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_MWDT1
  ret = esp_wdt_initialize("/dev/watchdog1", ESP_WDT_MWDT1);
  if (ret < 0)
    {
      _err("Failed to initialize WDT: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_RWDT
  ret = esp_wdt_initialize("/dev/watchdog2", ESP_WDT_RWDT);
  if (ret < 0)
    {
      _err("Failed to initialize WDT: %d\n", ret);
    }
#endif

#ifdef CONFIG_TIMER
  ret = esp_timer_initialize(0);
  if (ret < 0)
    {
      _err("Failed to initialize Timer 0: %d\n", ret);
    }

#ifndef CONFIG_ONESHOT
  ret = esp_timer_initialize(1);
  if (ret < 0)
    {
      _err("Failed to initialize Timer 1: %d\n", ret);
    }
#endif
#endif

#ifdef CONFIG_ONESHOT
  ret = esp_oneshot_initialize();
  if (ret < 0)
    {
      _err("Failed to initialize Oneshot Timer: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESP_RMT
  ret = board_rmt_txinitialize(RMT_OUTPUT_PIN);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_rmt_txinitialize() failed: %d\n", ret);
    }

  ret = board_rmt_rxinitialize(RMT_INPUT_PIN);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_rmt_txinitialize() failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_RTC_DRIVER
  /* Initialize the RTC driver */

  ret = esp_rtc_driverinit();
  if (ret < 0)
    {
      _err("Failed to initialize the RTC driver: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_SPI
#  if defined(CONFIG_ESPRESSIF_SPI2_SLAVE) && defined(CONFIG_ESPRESSIF_SPI2)
  ret = board_spislavedev_initialize(ESPRESSIF_SPI2);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize SPI%d Slave driver: %d\n",
             ESPRESSIF_SPI2, ret);
    }
#  elif defined(CONFIG_ESPRESSIF_SPI2)
  ret = board_spidev_initialize(ESPRESSIF_SPI2);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to init spidev 2: %d\n", ret);
    }
#  endif

#  if defined(CONFIG_ESPRESSIF_SPI3_SLAVE) && defined(CONFIG_ESPRESSIF_SPI3)
  ret = board_spislavedev_initialize(ESPRESSIF_SPI3);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize SPI%d Slave driver: %d\n",
             ESPRESSIF_SPI3, ret);
    }
#  elif defined(CONFIG_ESPRESSIF_SPI3)
  ret = board_spidev_initialize(ESPRESSIF_SPI3);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to init spidev 3: %d\n", ret);
    }
#  endif

#  ifdef CONFIG_ESPRESSIF_SPI_BITBANG
  ret = board_spidev_initialize(ESPRESSIF_SPI_BITBANG);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to init spidev 3: %d\n", ret);
    }
#  endif /* CONFIG_ESPRESSIF_SPI_BITBANG */

#  ifdef CONFIG_ESPRESSIF_LPSPI0
  ret = board_spidev_initialize(ESPRESSIF_LPSPI0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to init lpspi: %d\n", ret);
    }
#  endif
#endif /* CONFIG_ESPRESSIF_SPI */

#ifdef CONFIG_ESPRESSIF_SPIFLASH
  ret = board_spiflash_init();
  if (ret)
    {
      syslog(LOG_ERR, "ERROR: Failed to initialize SPI Flash\n");
    }
#endif

#if defined(CONFIG_ESPRESSIF_I2S)
  /* Configure I2S peripheral interfaces */

  ret = board_i2s_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize I2S driver: %d\n", ret);
    }
#endif

#if defined(CONFIG_I2C_DRIVER)
  /* Configure I2C peripheral interfaces */

  ret = board_i2c_init();

  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize I2C driver: %d\n", ret);
    }
#endif

#ifdef CONFIG_SENSORS_BMP180
  /* Try to register BMP180 device in I2C0 */

  ret = board_bmp180_initialize(0);

  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize BMP180 "
             "Driver for I2C0: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESP_SDM
  struct esp_sdm_chan_config_s config =
  {
    .gpio_num = 5,
    .sample_rate_hz = 1000 * 1000,
    .flags = 0,
  };

  struct dac_dev_s *dev = esp_sdminitialize(config);
  ret = dac_register("/dev/dac0", dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize DAC driver: %d\n",
             ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_TEMP
  struct esp_temp_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG(10, 50);
  ret = esp_temperature_sensor_initialize(cfg);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to initialize temperature sensor driver: %d\n",
             ret);
    }
#endif
#ifdef CONFIG_ESPRESSIF_TWAI0

  /* Initialize TWAI and register the TWAI driver. */

  ret = board_twai_setup(0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: TWAI0 board_twai_setup failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_TWAI1

  /* Initialize TWAI and register the TWAI driver. */

  ret = board_twai_setup(1);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: TWAI1 board_twai_setup failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_TWAI2

  /* Initialize TWAI and register the TWAI driver. */

  ret = board_twai_setup(2);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: TWAI2 board_twai_setup failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_DEV_GPIO
  ret = esp_gpio_init();
  if (ret < 0)
    {
      ierr("Failed to initialize GPIO Driver: %d\n", ret);
    }
#endif

#if defined(CONFIG_INPUT_BUTTONS) && defined(CONFIG_INPUT_BUTTONS_LOWER)
  /* Register the BUTTON driver */

  ret = btn_lower_initialize("/dev/buttons");
  if (ret < 0)
    {
      ierr("ERROR: btn_lower_initialize() failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_LEDC
  ret = board_ledc_setup();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_ledc_setup() failed: %d\n", ret);
    }
#endif /* CONFIG_ESPRESSIF_LEDC */

#ifdef CONFIG_ESP_MCPWM_CAPTURE
  ret = board_capture_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_capture_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESP_MCPWM_MOTOR
  ret = board_motor_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_motor_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESP_PCNT
  ret = board_pcnt_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_pcnt_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_PM
  /* Configure PM */

  ret = esp_pmconfigure();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: esp_pmconfigure failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_SYSTEM_NXDIAG_ESPRESSIF_CHIP_WO_TOOL
  ret = esp_nxdiag_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: esp_nxdiag_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_ADC
  ret = board_adc_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_adc_init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_ANA_COMPR0
  ret = esp_cmprinitialize(ESPRESSIF_COMP0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: esp_cmprinitialize(%d) failed: %d\n",
             ESPRESSIF_COMP0, ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_ANA_COMPR1
  ret = esp_cmprinitialize(ESPRESSIF_COMP1);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: esp_cmprinitialize(%d) failed: %d\n",
             ESPRESSIF_COMP1, ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_EMAC
  ret = board_emac_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: board_emac_init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESPRESSIF_USE_LP_CORE
#  ifdef CONFIG_ESPRESSIF_LP_MAILBOX
  esp_lp_mailbox_init();
#  endif

  /* ULP initialization should be the handled later than
   * peripherals to use supported peripherals properly on ULP core
   */

  ret = esp_ulp_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: esp_ulp_init failed: %d\n", ret);
    }
  else
    {
#  ifdef CONFIG_ESPRESSIF_ULP_USE_TEST_BIN
      esp_ulp_load_bin((char *)esp_ulp_bin, esp_ulp_bin_len);
#  endif
    }
#endif

  /* If we got here then perhaps not all initialization was successful, but
   * at least enough succeeded to bring-up NSH with perhaps reduced
   * capabilities.
   */

  bringup_progress("A8\n");
  return ret;
}
