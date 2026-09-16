/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include "espressif/esp_i2c.h"
#include "espressif/esp_i2s.h"
#include "espressif/esp_gpio.h"
#include "board_microphone.h"

#if CONFIG_ESPRESSIF_I2C0_SDAPIN != 7 || CONFIG_ESPRESSIF_I2C0_SCLPIN != 8 || \
    CONFIG_ESPRESSIF_I2S0_BCLKPIN != 12 || CONFIG_ESPRESSIF_I2S0_WSPIN != 10 || \
    CONFIG_ESPRESSIF_I2S0_DINPIN != 11 || CONFIG_ESPRESSIF_I2S0_MCLKPIN != 13 || \
    CONFIG_ESPRESSIF_I2S0_DOUTPIN != 9
#error "Microphone requires the Function EV Board audio pin mapping"
#endif

/* Called once by board bringup after the scheduler and I2C are available. */
int board_microphone_initialize(void)
{
  struct i2c_master_s *i2c = esp_i2cbus_initialize(0);
  if (!i2c) return -ENODEV;
  struct i2s_dev_s *i2s = esp_i2sbus_initialize(0);
  if (!i2s) return -ENODEV;
  int ret = board_microphone_register(i2c, i2s, 0x18, 400000);
  if (ret < 0) return ret;
  ret = esp_configgpio(53, OUTPUT);
  if (ret < 0) return ret;
  esp_gpiowrite(53, false);
  return OK;
}

/* ES32-P4 Function EV Board, matching Espressif BSP_POWER_AMP_IO.
 * Called by the owner of the shared ES8311 playback reservation only. */
void board_music_amplifier(bool enabled)
{
  esp_gpiowrite(53, enabled);
  printf("[audio-state] amplifier request=%d\n", enabled);
}
