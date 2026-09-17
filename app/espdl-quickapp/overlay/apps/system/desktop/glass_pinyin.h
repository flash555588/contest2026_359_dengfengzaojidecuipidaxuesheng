/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
bool glass_pinyin_init(void);
void glass_pinyin_reset(void);
unsigned glass_pinyin_search(const char *input);
bool glass_pinyin_candidate(unsigned index, char *out, size_t capacity);
bool glass_pinyin_choose(unsigned index, char *out, size_t capacity, unsigned *consumed);
void glass_pinyin_flush(void);
void glass_pinyin_close(void);
#ifdef __cplusplus
}
#endif
