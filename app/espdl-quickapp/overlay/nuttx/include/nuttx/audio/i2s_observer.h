/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <nuttx/fs/ioctl.h>
/* Stop RX DMA only and synchronously retire all RX callbacks. TX clocks and
 * playback ownership are retained. Called by the current duplex owner. */
#define I2SIOC_RXSTOP _AUDIOIOC(0x7d)
