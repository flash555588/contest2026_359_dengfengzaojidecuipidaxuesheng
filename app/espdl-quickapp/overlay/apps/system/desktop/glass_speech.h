/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum speech_phase {SPEECH_IDLE,SPEECH_LOADING,SPEECH_WAKE,SPEECH_COMMAND,SPEECH_SPEAKING,SPEECH_STOPPING,SPEECH_ERROR};
struct speech_snapshot {
  uint32_t revision;enum speech_phase phase;bool active;int error,command;
  unsigned level;char text[128];
};
int glass_speech_listen(void);
int glass_speech_say(const char *text);
void glass_speech_stop(void);
void glass_speech_get(struct speech_snapshot *out);
int glass_speech_cli(int argc,char **argv);
