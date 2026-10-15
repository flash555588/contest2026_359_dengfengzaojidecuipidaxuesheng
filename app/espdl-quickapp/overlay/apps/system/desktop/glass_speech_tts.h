/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef bool (*speech_cancel_fn)(void *user);
typedef int (*speech_pcm_fn)(void *user, const int16_t *pcm, size_t frames);
/* Worker thread only. SD models, G2P and arenas are released before return.
 * PCM is mono 22050 Hz. Text is phonemized once, then rendered in bounded
 * syllable groups; no inference or file I/O is performed on the LVGL thread. */
int glass_sano_synthesize(const char *root, const char *text,
                          speech_pcm_fn write_pcm, speech_cancel_fn cancel,
                          void *user);
