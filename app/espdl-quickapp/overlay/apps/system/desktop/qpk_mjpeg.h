/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
void *qpk_mjpeg_create(void);
void qpk_mjpeg_destroy(void *decoder);
/* Read the pixel size of a JPEG frame without decoding it. */
int qpk_mjpeg_probe(const uint8_t *input, size_t size, unsigned *width,
                    unsigned *height);
/* Valid after successful decode; includes any inserted standard DHT. */
size_t qpk_mjpeg_frame_size(void *decoder);
int qpk_mjpeg_decode(void *decoder, uint8_t *input, size_t size, size_t capacity,
                     uint16_t *output, unsigned width, unsigned height,
                     unsigned expected_width, unsigned expected_height);
