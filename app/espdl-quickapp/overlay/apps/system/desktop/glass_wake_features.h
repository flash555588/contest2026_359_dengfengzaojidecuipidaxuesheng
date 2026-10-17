/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define GLASS_WAKE_RATE 16000
#define GLASS_WAKE_STEPS 32
#define GLASS_WAKE_COEFS 13
#define GLASS_WAKE_INPUTS (GLASS_WAKE_STEPS*GLASS_WAKE_COEFS)
#define GLASS_WAKE_POSITIVE 54
#define GLASS_WAKE_NEGATIVE 144
#define GLASS_WAKE_TEMPLATES (GLASS_WAKE_POSITIVE+GLASS_WAKE_NEGATIVE)
#define GLASS_WAKE_MAX_SAMPLES 64000
#define GLASS_WAKE_WEIGHTS (GLASS_WAKE_TEMPLATES*GLASS_WAKE_INPUTS)
/* Shared by the host trainer and board; returns zero or a negative errno. */
int glass_wake_extract(const int16_t *pcm,size_t samples,float out[GLASS_WAKE_INPUTS]);
float glass_wake_ratio(const float *templates,const float features[GLASS_WAKE_INPUTS]);
float glass_wake_dtw(const float *a,const float *b);
