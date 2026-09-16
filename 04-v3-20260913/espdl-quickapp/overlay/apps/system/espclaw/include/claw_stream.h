/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum claw_stream_kind { CLAW_STREAM_CONNECT, CLAW_STREAM_ACTIVITY, CLAW_STREAM_TEXT,
  CLAW_STREAM_REASONING, CLAW_STREAM_TOOL, CLAW_STREAM_USAGE, CLAW_STREAM_LIMIT };
struct claw_stream_event {
  enum claw_stream_kind kind;
  const char *text;
  uint32_t output_tokens, reasoning_tokens;
  bool has_output_tokens, has_reasoning_tokens;
};
typedef void (*claw_stream_observer)(const struct claw_stream_event *, void *);
/* Bind inside on_request_start: each core worker owns its own observer. */
void claw_stream_bind(claw_stream_observer observer, void *user);
void claw_stream_emit(enum claw_stream_kind kind, const char *text);
struct claw_stream;
struct claw_stream *claw_stream_create(bool anthropic);
int claw_stream_feed(struct claw_stream *stream, const char *data, size_t size);
int claw_stream_finish(struct claw_stream *stream, char **body);
const char *claw_stream_error(const struct claw_stream *stream);
void claw_stream_free(struct claw_stream *stream);
