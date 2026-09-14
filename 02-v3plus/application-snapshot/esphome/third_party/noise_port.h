/* SPDX-License-Identifier: MIT */
#ifndef OPENVELA_ESPHOME_NOISE_PORT_H
#define OPENVELA_ESPHOME_NOISE_PORT_H
#include <stddef.h>

/* Noise error code; never returns success after an entropy-source failure. */
int esphome_noise_random(void *bytes, size_t size);
#endif
