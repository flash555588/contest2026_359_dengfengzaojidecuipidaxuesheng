/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "qpk_storage.h"
#include <stdbool.h>
#include <stdatomic.h>
#include <cJSON.h>

int qpk_audio_path(const char *root, const char *package, const char *clip,
                   char path[QPK_STORAGE_PATH_MAX], bool create);
int qpk_audio_info(const char *path, cJSON *out);
int qpk_audio_list(const char *root, const char *package, cJSON *out,
                   const atomic_bool *cancel);
