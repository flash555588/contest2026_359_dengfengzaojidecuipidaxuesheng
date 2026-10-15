/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define GLASS_SPEECH_MAX_COMMANDS 16
struct speech_command {int id;char text[96];char pinyin[96];};
struct glass_sr;
/* Single recognition worker owns the global upstream model registry. */
int glass_sr_create(const char *root,const struct speech_command *commands,
                     size_t count,struct glass_sr **out);
/* 16000 Hz signed mono. Returns 0/no event, 1/wake, 2/command, 3/timeout, <0/error.
 * Commands are emitted as events only; this module cannot control HA. */
int glass_sr_feed(struct glass_sr *sr,const int16_t *pcm,size_t frames,int *command);
void glass_sr_destroy(struct glass_sr *sr);
