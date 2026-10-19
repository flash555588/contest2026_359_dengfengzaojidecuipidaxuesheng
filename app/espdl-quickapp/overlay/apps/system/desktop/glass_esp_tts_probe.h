/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
int glass_esp_tts_probe(int mode,bool (*cancel)(void *),void *user);
