/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT (1U<<2)
#define MALLOC_CAP_INTERNAL (1U<<11)
#define MALLOC_CAP_SPIRAM (1U<<10)
#define MALLOC_CAP_DEFAULT (1U<<12)
void *heap_caps_malloc(size_t bytes,uint32_t caps);
void *heap_caps_calloc(size_t count,size_t bytes,uint32_t caps);
void *heap_caps_aligned_alloc(size_t alignment,size_t bytes,uint32_t caps);
void heap_caps_free(void *p);
