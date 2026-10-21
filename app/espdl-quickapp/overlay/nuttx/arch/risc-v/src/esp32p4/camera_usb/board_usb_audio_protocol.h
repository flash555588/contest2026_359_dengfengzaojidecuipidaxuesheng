/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <nuttx/usb/board_usb.h>
int board_usb_audio_parse(const uint8_t *,size_t,unsigned,struct board_usb_info *);
