/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdbool.h>
/* Relative paths must be present in the generated pinned manifest. */
void *glass_speech_asset(const char *root,const char *relative,size_t *bytes);
void *glass_speech_asset_cancelable(const char *root,const char *relative,size_t *bytes,
                                   bool (*cancel)(void *),void *user);
