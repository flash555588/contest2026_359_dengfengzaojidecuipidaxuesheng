/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "glass_speech_tts.h"
/* Fixed official English phoneme fixture; diagnostic only, no text frontend. */
int glass_nano_probe(int mode,speech_cancel_fn cancel,void *user);
