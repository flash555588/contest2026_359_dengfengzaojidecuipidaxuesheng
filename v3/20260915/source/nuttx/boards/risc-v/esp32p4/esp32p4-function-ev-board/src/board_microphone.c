/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/audio/audio.h>
#include <nuttx/audio/es8311.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mutex.h>
#include <errno.h>
#include <stdbool.h>

/* Call after board bus initialization, with the board's shared I2C instance.
 * Bus ownership remains with the board, including on registration failure.
 * One physical codec has one persistent lower half, not separate ADC/DAC
 * instances resetting each other's hardware state.
 */
static mutex_t g_mic_lock = NXMUTEX_INITIALIZER;
static struct audio_lowerhalf_s *g_microphone;
static struct i2c_master_s *g_mic_i2c;
static struct i2s_dev_s *g_mic_i2s;
static struct es8311_lower_s g_mic_config;
static bool g_mic_registered;

int board_microphone_register(struct i2c_master_s *i2c,
                              struct i2s_dev_s *i2s,
                              uint8_t address, uint32_t frequency)
{
  if (!i2c || !i2s || address > 0x7f || address == 0 ||
      frequency == 0 || frequency > 400000) return -EINVAL;
  int ret = nxmutex_lock(&g_mic_lock);
  if (ret < 0) return ret;
  if (g_microphone && (g_mic_i2c != i2c || g_mic_i2s != i2s ||
                      g_mic_config.address != address ||
                      g_mic_config.frequency != frequency))
    {
      ret = -EBUSY;
      goto out;
    }
  if (g_mic_registered)
    {
      ret = 0;
      goto out;
    }
  if (!g_microphone)
    {
      uint8_t reg = 0;
      uint8_t value;
      struct i2c_msg_s messages[2] =
      {
        {.frequency = frequency, .addr = address, .flags = 0,
         .buffer = &reg, .length = 1},
        {.frequency = frequency, .addr = address, .flags = I2C_M_READ,
         .buffer = &value, .length = 1}
      };
      /* Read the reset register without modifying codec state. An ACK is
       * a presence check only, not proof of codec identity or working ADC. */
      ret = I2C_TRANSFER(i2c, messages, 2);
      if (ret < 0) goto out;
      g_mic_config.address = address;
      g_mic_config.frequency = frequency;
      g_microphone = es8311_initialize(i2c, i2s, &g_mic_config);
      if (!g_microphone)
        {
          ret = -ENOMEM;
          goto out;
        }
      g_mic_i2c = i2c;
      g_mic_i2s = i2s;
    }
  ret = audio_register("pcm_in0", g_microphone);
  if (ret == 0) g_mic_registered = true;
out:
  nxmutex_unlock(&g_mic_lock);
  return ret;
}
