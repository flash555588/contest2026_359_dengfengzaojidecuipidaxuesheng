/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Match the chat input limit; never submit a shortened transcript. */
#define GLASS_VOICE_TEXT_BYTES 1536
#define GLASS_VOICE_MAX_MS 60000u

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
  GLASS_VOICE_ERROR_RESPONSE,
  GLASS_VOICE_ERROR_INCOMPLETE,
  GLASS_VOICE_ERROR_TOO_LONG
};

struct glass_voice_snapshot {
  uint32_t revision,request;
  enum glass_voice_phase phase;
  enum glass_voice_error error;
  unsigned level;
  bool active;
  bool wake_observing;
  char text[GLASS_VOICE_TEXT_BYTES + 1];
};

/* New requests preempt old capture/transport asynchronously. */
int glass_voice_start(void);
int glass_voice_start_request(uint32_t *request);
void glass_voice_stop_request(uint32_t request);
void glass_voice_cancel(void);
/* Includes retired workers still closing their sockets/capture after cancel. */
bool glass_voice_config_idle(void);
void glass_voice_stop(void);
void glass_voice_get(struct glass_voice_snapshot *out);
const char *glass_voice_error_text(enum glass_voice_error error);
