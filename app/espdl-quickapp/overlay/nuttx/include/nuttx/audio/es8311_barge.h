/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/fs/ioctl.h>
/* An observer of the ADC on an already reserved 16kHz playback session.
 * It does not reserve/reset/start/stop the codec or change its clock. The
 * playback owner must stop/join its observer before AUDIOIOC_STOP/RELEASE. */
#define ES8311IOC_BARGEIN _AUDIOIOC(0x7e)
struct es8311_bargein { struct i2s_dev_s *i2s; unsigned rate; };
