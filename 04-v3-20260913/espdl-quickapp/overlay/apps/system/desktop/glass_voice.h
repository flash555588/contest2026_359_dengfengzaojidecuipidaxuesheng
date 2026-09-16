/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum glass_voice_phase {
  GLASS_VOICE_IDLE,
  GLASS_VOICE_CONNECTING,
  GLASS_VOICE_LISTENING,
  GLASS_VOICE_FINISHING,
  GLASS_VOICE_DONE,
  GLASS_VOICE_ERROR
};

enum glass_voice_error {
  GLASS_VOICE_ERROR_NONE,
  GLASS_VOICE_ERROR_CONFIG,
  GLASS_VOICE_ERROR_CLOCK,
  GLASS_VOICE_ERROR_NETWORK,
  GLASS_VOICE_ERROR_TLS,
  GLASS_VOICE_ERROR_AUTH,
  GLASS_VOICE_ERROR_AUDIO,
  GLASS_VOICE_ERROR_EMPTY,
  GLASS_VOICE_ERROR_MEMORY,
  GLASS_VOICE_ERROR_RESPONSE
};

struct glass_voice_snapshot {
  uint32_t revision;
  enum glass_voice_phase phase;
  enum glass_voice_error error;
  unsigned level;
  bool active;
  char text[768];
};

int glass_voice_start(void);
void glass_voice_stop(void);
void glass_voice_get(struct glass_voice_snapshot *out);
const char *glass_voice_error_text(enum glass_voice_error error);
