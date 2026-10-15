/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
/* Relative paths must be present in the generated pinned manifest. */
void *glass_speech_asset(const char *root,const char *relative,size_t *bytes);
