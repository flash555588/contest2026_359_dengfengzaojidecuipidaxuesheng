/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_DECODER_H
#define BILI_DECODER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*bili_frame_fn)(void *, const uint8_t *const planes[3],
                             const int strides[2], int width, int height);
void *bili_decoder_create(bili_frame_fn frame, void *opaque);
int bili_decoder_feed(void *decoder, const uint8_t *data, size_t size);
typedef int (*bili_timed_frame_fn)(void *, const uint8_t *const planes[3],
                                  const int strides[2], int width, int height,
                                  uint64_t timestamp_ms);
void *bili_decoder_create_timed(bili_timed_frame_fn frame, void *opaque);
int bili_decoder_feed_timestamp(void *decoder, const uint8_t *, size_t, uint64_t);
int bili_decoder_flush(void *decoder);
void bili_decoder_destroy(void *decoder);
int bili_decoder_set_mc_features(unsigned features);
#ifdef __cplusplus
}
#endif
#endif
