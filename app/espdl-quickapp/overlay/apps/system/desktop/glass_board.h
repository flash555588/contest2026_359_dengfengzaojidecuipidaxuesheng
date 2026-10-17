/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cJSON.h>
#include <stdatomic.h>
/* Called from an agent worker. UI work is marshalled to the desktop thread. */
char *glass_board_ui(const char *operation, const cJSON *arguments, atomic_bool *cancel);
