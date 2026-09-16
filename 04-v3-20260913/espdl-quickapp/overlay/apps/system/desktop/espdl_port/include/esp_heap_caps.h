/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT (1U << 2)
#define MALLOC_CAP_DMA (1U << 3)
#define MALLOC_CAP_SPIRAM (1U << 10)
#define MALLOC_CAP_INTERNAL (1U << 11)
#define MALLOC_CAP_DEFAULT (1U << 12)
#define MALLOC_CAP_TCM (1U << 17)
#define MALLOC_CAP_SIMD (1U << 18)
#define HEAP_IRAM_ATTR
#ifdef __cplusplus
extern "C" {
#endif
void *qpk_dl_malloc(size_t size, uint32_t caps);
void *qpk_dl_calloc(size_t n, size_t size, uint32_t caps);
void *qpk_dl_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void *qpk_dl_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps);
void qpk_dl_free(void *ptr);
size_t qpk_dl_free_size(uint32_t caps);
size_t qpk_dl_largest_block(uint32_t caps);
#ifdef __cplusplus
}
#endif
#define heap_caps_malloc qpk_dl_malloc
#define heap_caps_calloc qpk_dl_calloc
#define heap_caps_aligned_alloc qpk_dl_aligned_alloc
#define heap_caps_aligned_calloc qpk_dl_aligned_calloc
#define heap_caps_free qpk_dl_free
#define heap_caps_get_free_size qpk_dl_free_size
#define heap_caps_get_largest_free_block qpk_dl_largest_block
