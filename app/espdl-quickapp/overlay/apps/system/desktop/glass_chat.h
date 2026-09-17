/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHAT_MAX_TURNS 6
#define CHAT_PROMPT_BYTES 1536
#define CHAT_REPLY_BYTES 8192
#define CHAT_CONTEXT_BYTES 12288

enum chat_turn_state { CHAT_WAITING, CHAT_DONE, CHAT_FAILED, CHAT_CANCELLED };
enum chat_phase { CHAT_LOADING, CHAT_IDLE, CHAT_QUEUED, CHAT_REQUESTING,
                  CHAT_STOPPING, CHAT_CLEARING };
enum chat_error {
  CHAT_ERROR_NONE, CHAT_ERROR_CONFIG, CHAT_ERROR_NETWORK, CHAT_ERROR_TIMEOUT,
  CHAT_ERROR_AUTH, CHAT_ERROR_MODEL, CHAT_ERROR_LIMIT, CHAT_ERROR_TLS,
  CHAT_ERROR_RESPONSE, CHAT_ERROR_MEMORY, CHAT_ERROR_INTERRUPTED,
  CHAT_ERROR_CLEANUP, CHAT_ERROR_OUTPUT_LIMIT, CHAT_ERROR_STREAM
};

struct chat_turn {
  uint32_t id;
  enum chat_turn_state state;
  enum chat_error error;
  bool clipped;
  uint32_t text_bytes, reasoning_bytes, output_tokens, reasoning_tokens;
  uint32_t began_ms, activity_ms;
  unsigned stream_round;
  bool has_output_tokens, has_reasoning_tokens, output_limited;
  char progress[96], reasoning[1025];
  char archive[48];
  uint32_t archive_bytes;
  char prompt[CHAT_PROMPT_BYTES + 1];
  char reply[CHAT_REPLY_BYTES + 1];
};

struct chat_snapshot {
  unsigned revision, count;
  enum chat_phase phase;
  bool loaded, configured;
  int storage_error;
  char model[129];
  struct chat_turn turns[CHAT_MAX_TURNS];
};

int glass_chat_start(void);
void glass_chat_refresh_config(void);
int glass_chat_send(const char *prompt);
int glass_chat_retry(void);
void glass_chat_cancel(void);
int glass_chat_clear(void);
/* Copies only when revision changes; callers keep their snapshot off-stack. */
bool glass_chat_get(struct chat_snapshot *out);
const char *glass_chat_error_text(enum chat_error error);
bool glass_chat_utf8(const char *text, size_t limit, bool multiline);
size_t glass_chat_copy_utf8(char *out, size_t capacity, const char *text);

/* File access is owned by the chat worker. No LVGL calls in these functions. */
int glass_chat_store_load(struct chat_snapshot *out);
int glass_chat_store_save(const struct chat_snapshot *state);
char *glass_chat_context(const struct chat_snapshot *state);
int glass_chat_archive(const char *text, uint32_t id, char name[48], uint32_t *bytes);
#define CHAT_PAGE_BYTES 4096
struct chat_page { unsigned revision, page, pages; bool busy; int error; char text[CHAT_PAGE_BYTES + 1]; };
int glass_chat_page_request(const char *name, unsigned page);
bool glass_chat_page_get(struct chat_page *out);
