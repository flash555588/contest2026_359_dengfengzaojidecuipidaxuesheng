/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include "glass_agent_mode.h"

#define CHAT_MAX_TURNS 6
#define CHAT_PROMPT_BYTES 1536
#define CHAT_REPLY_BYTES 8192
#define CHAT_CONTEXT_BYTES 12288
#define CHAT_TOOL_TRACE 4
#define CHAT_CARD_COUNT 4
#define CHAT_CARD_TEXT 384

enum chat_turn_state { CHAT_WAITING, CHAT_DONE, CHAT_FAILED, CHAT_CANCELLED };
enum chat_phase { CHAT_LOADING, CHAT_IDLE, CHAT_QUEUED, CHAT_REQUESTING,
                  CHAT_STOPPING, CHAT_CLEARING };
enum chat_error {
  CHAT_ERROR_NONE, CHAT_ERROR_CONFIG, CHAT_ERROR_NETWORK, CHAT_ERROR_TIMEOUT,
  CHAT_ERROR_AUTH, CHAT_ERROR_MODEL, CHAT_ERROR_LIMIT, CHAT_ERROR_TLS,
  CHAT_ERROR_RESPONSE, CHAT_ERROR_MEMORY, CHAT_ERROR_INTERRUPTED,
  CHAT_ERROR_CLEANUP, CHAT_ERROR_OUTPUT_LIMIT, CHAT_ERROR_STREAM, CHAT_ERROR_CAMERA,
  CHAT_ERROR_CAMERA_STORAGE
};

/* One device result card rendered inside the dialog; already validated. */
struct chat_card {
  char entity[96];
  char kind[12];
  char name[64];
  char state[64];
  int level;
  bool on;
  bool action,verified;
};

struct chat_turn {
  uint32_t id;
  enum glass_agent_mode mode;
  enum chat_turn_state state;
  enum chat_error error;
  bool clipped;
  bool camera_samples; /* Explicit UI upload; never automatically retried. */
  bool session_history; /* Bot turns use independent SD receipts. */
  bool camera_tool_used; /* One physical observation attempt per Agent turn. */
  int camera_save_error;
  char camera_observation[128];
  char camera_recording[128]; /* Completed SD bundle, retained on AI failure. */
  uint32_t text_bytes, reasoning_bytes, output_tokens, reasoning_tokens;
  uint32_t began_ms, activity_ms;
  unsigned stream_round;
  bool has_output_tokens, has_reasoning_tokens, output_limited;
  char progress[96], reasoning[1025];
  unsigned tool_count,tool_total;
  char tools[CHAT_TOOL_TRACE][40];
  unsigned card_count;
  struct chat_card cards[CHAT_CARD_COUNT];
  char archive[48];
  uint32_t archive_bytes;
  char prompt[CHAT_PROMPT_BYTES + 1];
  char reply[CHAT_REPLY_BYTES + 1];
};

struct chat_snapshot {
  unsigned revision, count;
  enum glass_agent_mode active_mode;
  /* Caller-local filter marker; not persisted or sent to the model. */
  enum glass_agent_mode view_mode;
  enum chat_phase phase;
  bool loaded, configured;
  int storage_error;
  char model[129];
  struct chat_turn turns[CHAT_MAX_TURNS];
};

int glass_chat_start(void);
void glass_chat_refresh_config(void);
int glass_chat_send(const char *prompt);
int glass_chat_send_mode(const char *prompt,enum glass_agent_mode mode);
int glass_chat_send_bot(const char *prompt,enum glass_agent_mode mode);
int glass_chat_send_bot_camera(const char *prompt,enum glass_agent_mode mode);
char *glass_chat_observe_tool(const char *question,atomic_bool *cancel);
/* Explicit user action, with no tools enabled for image content. */
int glass_chat_send_camera(const char *question,enum glass_agent_mode mode);
int glass_chat_retry_mode(enum glass_agent_mode mode);
int glass_chat_clear_mode(enum glass_agent_mode mode);
bool glass_chat_get_mode(struct chat_snapshot *out,enum glass_agent_mode mode);
int glass_chat_retry(void);
void glass_chat_cancel(void);
bool glass_chat_config_idle(void);
void glass_chat_config_reload(void);
int glass_chat_clear(void);
/* Copies only when revision changes; callers keep their snapshot off-stack. */
bool glass_chat_get(struct chat_snapshot *out);
/* A small typed projection for passive UI companions, without copying history
 * or starting a chat/network worker. Read under the service mutex. */
enum chat_activity { CHAT_ACTIVITY_CONNECT, CHAT_ACTIVITY_THINK,
                     CHAT_ACTIVITY_REPLY, CHAT_ACTIVITY_TOOL };
struct chat_activity_snapshot {
  enum chat_phase phase;
  enum chat_activity activity;
  enum chat_turn_state state;
  uint32_t turn_id;
  bool loaded, configured;
};
void glass_chat_activity_get(struct chat_activity_snapshot *out);
/* Bounded result projection; never copies history or performs I/O. */
struct chat_result_snapshot {
  uint32_t revision,turn_id,next_offset;
  enum chat_phase phase;enum chat_turn_state state;enum chat_error error;
  unsigned card_count;bool more;
  char progress[96],reply[241];
  struct chat_card cards[CHAT_CARD_COUNT];
};
bool glass_chat_result_get(struct chat_result_snapshot *out,uint32_t id,
                          enum glass_agent_mode mode,size_t offset);
const char *glass_chat_error_text(enum chat_error error);
bool glass_chat_utf8(const char *text, size_t limit, bool multiline);
size_t glass_chat_copy_utf8(char *out, size_t capacity, const char *text);

/* File access is owned by the chat worker. No LVGL calls in these functions. */
int glass_chat_store_load(struct chat_snapshot *out);
int glass_chat_store_save(const struct chat_snapshot *state);
char *glass_chat_context(const struct chat_snapshot *state);
int glass_chat_archive(const char *text, uint32_t id, char name[48], uint32_t *bytes);
int glass_chat_archive_session(const char *text,uint32_t id,char name[48],uint32_t *bytes);
#define CHAT_PAGE_BYTES 4096
struct chat_page { unsigned revision, page, pages; bool busy; int error; char text[CHAT_PAGE_BYTES + 1]; };
int glass_chat_page_request(const char *name, unsigned page);
bool glass_chat_page_get(struct chat_page *out);
