/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_SPS_H
#define BILI_SPS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int bili_sps_dimensions(const uint8_t *nal, size_t size, int *w, int *h);
#ifdef __cplusplus
}
#endif
#endif
