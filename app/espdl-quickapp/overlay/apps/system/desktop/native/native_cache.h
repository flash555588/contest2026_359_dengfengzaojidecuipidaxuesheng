/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OV_NATIVE_CACHE_H
#define OV_NATIVE_CACHE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef bool (*ov_native_cache_sync_fn)(void *, uintptr_t, size_t, bool);
bool ov_native_cache_prepare(uintptr_t start, size_t bytes, size_t line,
	ov_native_cache_sync_fn sync, void *ctx);
#endif
