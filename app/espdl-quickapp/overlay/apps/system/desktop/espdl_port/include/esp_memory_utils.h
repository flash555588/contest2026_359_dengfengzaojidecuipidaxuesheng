/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
static inline bool esp_ptr_in_tcm(const void *p) { return false; }
static inline bool esp_ptr_external_ram(const void *p)
{ return (uintptr_t)p >= 0x48000000U && (uintptr_t)p < 0x4c000000U; }
static inline bool esp_ptr_in_drom(const void *p)
{ return (uintptr_t)p >= 0x40000000U && (uintptr_t)p < 0x44000000U; }
static inline bool esp_ptr_internal(const void *p)
{ return (uintptr_t)p >= 0x4ff00000U && (uintptr_t)p < 0x50000000U; }
