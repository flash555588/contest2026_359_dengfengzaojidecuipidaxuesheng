/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
struct i2c_master_s;
struct i2s_dev_s;
/* Persistent input registration. Configure AUDIO_TYPE_INPUT before capture.
 * Successful registration does not verify captured samples or ADC setup. */
int board_microphone_register(struct i2c_master_s *i2c,
                              struct i2s_dev_s *i2s,
                              uint8_t address, uint32_t frequency);
