/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cJSON.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

void glass_board_assistant_register(void);
int glass_assistant_start(void);
/* Also used by deterministic host tests; never exposed as a model tool. */
int glass_assistant_tick(time_t now);
bool glass_assistant_take_notice(char *out, size_t capacity);
int glass_assistant_notice(const char *text);

/* Platform boundaries: never call LVGL from a worker. Returned JSON is owned. */
char *glass_assistant_platform(const char *tool, const cJSON *args, atomic_bool *cancel);
bool glass_assistant_action_valid(const char *action, const cJSON *args);
char *glass_assistant_action(const char *action, const cJSON *args, atomic_bool *cancel);
