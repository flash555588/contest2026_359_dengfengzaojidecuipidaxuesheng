/* SPDX-License-Identifier: Apache-2.0
 * Incremental SSE decoder. HTTP/TLS fragmentation never becomes JSON framing.
 * Only completed tool arguments are returned to the existing agent core.
 */
#include "claw_stream.h"
#include <cJSON.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_LIMIT (SIZE_MAX - 1)
#define TEXT_LIMIT (2u * 1024 * 1024)
#define TOOL_LIMIT (SIZE_MAX - 1)
#define BLOCKS 16
struct buffer { char *data; size_t size, capacity; };
struct block { struct buffer id, name, args, text, signature; int kind; bool stopped; };
struct claw_stream {
  bool anthropic, done, finished, limited;
  struct buffer line, event, text, reasoning;
  struct block blocks[BLOCKS];
  const char *error;
};
struct observer_binding { claw_stream_observer fn; void *user; };
static pthread_key_t observer_key;
static pthread_once_t observer_once = PTHREAD_ONCE_INIT;
static int observer_error;
static void observer_init(void) { observer_error = pthread_key_create(&observer_key, free); }
static struct observer_binding *binding(void) {
  pthread_once(&observer_once, observer_init);
  return observer_error ? NULL : pthread_getspecific(observer_key);
}
void claw_stream_bind(claw_stream_observer fn, void *user) {
  struct observer_binding *b = binding();
  if (observer_error) return;
  if (!b) { b = calloc(1, sizeof(*b)); if (!b) return; if (pthread_setspecific(observer_key, b)) { free(b); return; } }
  b->fn = fn; b->user = user;
}
void claw_stream_emit(enum claw_stream_kind kind, const char *text)
{
  struct claw_stream_event event = {.kind = kind, .text = text};
  struct observer_binding *b = binding();
  if (b && b->fn) b->fn(&event, b->user);
}
static const char *string(cJSON *object, const char *key)
{
  cJSON *v = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
static int append(struct buffer *b, const char *text, size_t count, size_t limit)
{
  if (b->size > limit || count > limit - b->size) return -EFBIG;
  size_t need = b->size + count + 1;
  if (need > b->capacity) {
    size_t capacity = b->capacity ? b->capacity : 256;
    while (capacity < need) { if (capacity > SIZE_MAX / 2) { capacity = need; break; } capacity *= 2; }
    char *next = realloc(b->data, capacity);
    if (!next) return -ENOMEM;
    b->data = next; b->capacity = capacity;
  }
  memcpy(b->data + b->size, text, count); b->size += count; b->data[b->size] = 0;
  return 0;
}
static int add(struct buffer *b, const char *s, size_t limit) { return append(b, s, strlen(s), limit); }
static const char *value(struct buffer *b) { return b->data ? b->data : ""; }
static void clear(struct buffer *b) { b->size = 0; if (b->data) b->data[0] = 0; }
static void usage(cJSON *u)
{
  if (!cJSON_IsObject(u)) return;
  struct claw_stream_event event = {.kind = CLAW_STREAM_USAGE};
  cJSON *out = cJSON_GetObjectItemCaseSensitive(u, "completion_tokens");
  if (!out) out = cJSON_GetObjectItemCaseSensitive(u, "output_tokens");
  cJSON *details = cJSON_GetObjectItemCaseSensitive(u, "completion_tokens_details");
  if (!details) details = cJSON_GetObjectItemCaseSensitive(u, "output_tokens_details");
  cJSON *reason = cJSON_GetObjectItemCaseSensitive(details, "reasoning_tokens");
  if (!reason) reason = cJSON_GetObjectItemCaseSensitive(u, "reasoning_tokens");
  if (cJSON_IsNumber(out) && out->valuedouble >= 0 && out->valuedouble <= UINT32_MAX) {
    event.has_output_tokens = true; event.output_tokens = out->valuedouble;
  }
  if (cJSON_IsNumber(reason) && reason->valuedouble >= 0 && reason->valuedouble <= UINT32_MAX) {
    event.has_reasoning_tokens = true; event.reasoning_tokens = reason->valuedouble;
  }
  struct observer_binding *b = binding();
  if (b && b->fn && (event.has_output_tokens || event.has_reasoning_tokens)) b->fn(&event, b->user);
}
static int delta(struct buffer *b, const char *text, enum claw_stream_kind kind)
{
  if (!*text) return 0;
  int rc = add(b, text, TEXT_LIMIT);
  if (!rc) claw_stream_emit(kind, text);
  return rc;
}
static int index_of(cJSON *o)
{
  cJSON *i = cJSON_GetObjectItemCaseSensitive(o, "index");
  if (!cJSON_IsNumber(i) || i->valuedouble < 0 || i->valuedouble >= BLOCKS || i->valuedouble != i->valueint) return -1;
  return i->valueint;
}
static int openai_event(struct claw_stream *s, cJSON *root)
{
  usage(cJSON_GetObjectItemCaseSensitive(root, "usage"));
  cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
  if (!cJSON_IsArray(choices)) return -EPROTO;
  cJSON *choice = cJSON_GetArrayItem(choices, 0);
  if (!choice) return 0; /* final usage-only chunk */
  cJSON *d = cJSON_GetObjectItemCaseSensitive(choice, "delta");
  int rc = delta(&s->text, string(d, "content"), CLAW_STREAM_TEXT);
  if (!rc) rc = delta(&s->reasoning, string(d, "reasoning_content"), CLAW_STREAM_REASONING);
  if (rc) return rc;
  const char *finish = string(choice, "finish_reason");
  if (*finish) {
    s->finished = true;
    if (!strcmp(finish, "length")) { s->limited = true; claw_stream_emit(CLAW_STREAM_LIMIT, "length"); }
    else if (strcmp(finish, "stop") && strcmp(finish, "tool_calls")) return -EPROTO;
  }
  cJSON *calls = cJSON_GetObjectItemCaseSensitive(d, "tool_calls"), *call;
  cJSON_ArrayForEach(call, calls) {
    int i = index_of(call); if (i < 0) return -EPROTO;
    struct block *b = &s->blocks[i]; b->kind = 3;
    cJSON *f = cJSON_GetObjectItemCaseSensitive(call, "function");
    if ((rc = add(&b->id, string(call, "id"), 256)) ||
        (rc = add(&b->name, string(f, "name"), 128)) ||
        (rc = add(&b->args, string(f, "arguments"), TOOL_LIMIT))) return rc;
    if (*string(f, "name")) claw_stream_emit(CLAW_STREAM_TOOL, value(&b->name));
  }
  return 0;
}
static int anthropic_event(struct claw_stream *s, cJSON *root)
{
  const char *type = string(root, "type");
  if (!strcmp(type, "ping")) return 0;
  if (!strcmp(type, "message_start")) {
    usage(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "message"), "usage")); return 0;
  }
  if (!strcmp(type, "message_stop")) { s->done = true; return 0; }
  if (!strcmp(type, "message_delta")) {
    usage(cJSON_GetObjectItemCaseSensitive(root, "usage"));
    const char *reason = string(cJSON_GetObjectItemCaseSensitive(root, "delta"), "stop_reason");
    if (*reason) s->finished = true;
    if (!strcmp(reason, "max_tokens")) { s->limited = true; claw_stream_emit(CLAW_STREAM_LIMIT, "max_tokens"); }
    return 0;
  }
  int i = index_of(root); if (i < 0) return -EPROTO;
  struct block *b = &s->blocks[i]; int rc = 0;
  if (!strcmp(type, "content_block_start")) {
    if (b->kind) return -EPROTO;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "content_block");
    const char *kind = string(v, "type");
    b->kind = !strcmp(kind, "text") ? 1 : !strcmp(kind, "thinking") ? 2 : !strcmp(kind, "tool_use") ? 3 : 0;
    if (!b->kind) return -EPROTO;
    if (b->kind == 3) {
      if ((rc = add(&b->id, string(v, "id"), 256)) || (rc = add(&b->name, string(v, "name"), 128))) return rc;
      cJSON *input = cJSON_GetObjectItemCaseSensitive(v, "input");
      if (cJSON_IsObject(input) && input->child) {
        char *text = cJSON_PrintUnformatted(input); if (!text) return -ENOMEM;
        rc = add(&b->args, text, TOOL_LIMIT); free(text); if (rc) return rc;
      }
      claw_stream_emit(CLAW_STREAM_TOOL, value(&b->name));
    } else {
      const char *text = string(v, b->kind == 1 ? "text" : "thinking");
      rc = delta(&b->text, text, b->kind == 1 ? CLAW_STREAM_TEXT : CLAW_STREAM_REASONING);
    }
  } else if (!strcmp(type, "content_block_delta")) {
    if (!b->kind || b->stopped) return -EPROTO;
    cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "delta");
    const char *kind = string(d, "type");
    if (!strcmp(kind, "text_delta") && b->kind == 1) rc = delta(&b->text, string(d, "text"), CLAW_STREAM_TEXT);
    else if (!strcmp(kind, "thinking_delta") && b->kind == 2) rc = delta(&b->text, string(d, "thinking"), CLAW_STREAM_REASONING);
    else if (!strcmp(kind, "signature_delta") && b->kind == 2) rc = add(&b->signature, string(d, "signature"), TOOL_LIMIT);
    else if (!strcmp(kind, "input_json_delta") && b->kind == 3) rc = add(&b->args, string(d, "partial_json"), TOOL_LIMIT);
    else return -EPROTO;
  } else if (!strcmp(type, "content_block_stop")) {
    if (!b->kind || b->stopped) return -EPROTO;
    b->stopped = true;
  } else return -EPROTO;
  return rc;
}
static int event(struct claw_stream *s)
{
  if (!s->event.size) return 0;
  if (!strcmp(s->event.data, "[DONE]")) { s->done = true; clear(&s->event); return 0; }
  if (s->done) return -EPROTO;
  cJSON *root = cJSON_ParseWithOpts(s->event.data, NULL, true);
  clear(&s->event);
  if (!root) { s->error = "Failed to parse SSE event JSON"; return -EPROTO; }
  int rc;
  if (cJSON_GetObjectItemCaseSensitive(root, "error") || !strcmp(string(root, "type"), "error")) {
    s->error = "LLM stream reported an error"; rc = -EPROTO;
  } else rc = s->anthropic ? anthropic_event(s, root) : openai_event(s, root);
  cJSON_Delete(root); return rc;
}
struct claw_stream *claw_stream_create(bool anthropic)
{
  struct claw_stream *s = calloc(1, sizeof(*s)); if (s) s->anthropic = anthropic; return s;
}
int claw_stream_feed(struct claw_stream *s, const char *data, size_t size)
{
  for (size_t i = 0; i < size; i++) {
    char ch = data[i]; int rc = 0;
    if (!ch) return -EPROTO;
    if (ch != '\n') { rc = append(&s->line, &ch, 1, EVENT_LIMIT); }
    else {
      if (s->line.size && s->line.data[s->line.size - 1] == '\r') s->line.data[--s->line.size] = 0;
      if (!s->line.size) rc = event(s);
      else if (!strncmp(value(&s->line), "data:", 5)) {
        const char *v = s->line.data + 5; if (*v == ' ') v++;
        if (s->event.size) rc = add(&s->event, "\n", EVENT_LIMIT);
        if (!rc) rc = add(&s->event, v, EVENT_LIMIT);
      }
      clear(&s->line);
    }
    if (rc) { if (!s->error) s->error = rc == -EFBIG ? "LLM stream exceeds device memory limit" : rc == -ENOMEM ? "Out of memory receiving stream" : "Invalid LLM stream event"; return rc; }
  }
  return 0;
}
int claw_stream_finish(struct claw_stream *s, char **body)
{
  *body = NULL;
  if (!s->done || !s->finished || s->line.size || s->event.size) {
    s->error = "LLM stream interrupted before completion"; return -EPROTO;
  }
  cJSON *root = cJSON_CreateObject(), *message = NULL, *array = NULL;
  if (!root) return -ENOMEM;
  if (s->anthropic) { message = root; array = cJSON_AddArrayToObject(root, "content"); }
  else {
    cJSON *choices = cJSON_AddArrayToObject(root, "choices"), *choice = cJSON_CreateObject();
    cJSON_AddItemToArray(choices, choice); message = cJSON_AddObjectToObject(choice, "message");
    cJSON_AddStringToObject(message, "content", value(&s->text));
    cJSON_AddStringToObject(message, "reasoning_content", value(&s->reasoning));
    array = cJSON_AddArrayToObject(message, "tool_calls");
  }
  cJSON_AddStringToObject(message, "role", "assistant");
  int rc = 0; bool any = s->text.size > 0;
  for (unsigned i = 0; i < BLOCKS && !rc; i++) {
    struct block *b = &s->blocks[i]; if (!b->kind) continue;
    if (s->anthropic && !b->stopped) { rc = -EPROTO; break; }
    if (b->kind == 3 && s->limited) { s->error = "LLM output token limit interrupted tool arguments"; rc = -EFBIG; break; }
    cJSON *v = cJSON_CreateObject(); if (!v) { rc = -ENOMEM; break; }
    cJSON_AddItemToArray(array, v);
    if (b->kind == 3) {
      cJSON *args = cJSON_ParseWithOpts(b->args.size ? value(&b->args) : "{}", NULL, true);
      if (!*value(&b->id) || !*value(&b->name) || !cJSON_IsObject(args)) { cJSON_Delete(args); rc = -EPROTO; break; }
      cJSON_AddStringToObject(v, "id", value(&b->id)); any = true;
      if (s->anthropic) {
        cJSON_AddStringToObject(v, "type", "tool_use"); cJSON_AddStringToObject(v, "name", value(&b->name)); cJSON_AddItemToObject(v, "input", args);
      } else {
        cJSON_Delete(args); cJSON_AddStringToObject(v, "type", "function");
        cJSON *f = cJSON_AddObjectToObject(v, "function"); cJSON_AddStringToObject(f, "name", value(&b->name)); cJSON_AddStringToObject(f, "arguments", value(&b->args));
      }
    } else {
      cJSON_AddStringToObject(v, "type", b->kind == 1 ? "text" : "thinking");
      cJSON_AddStringToObject(v, b->kind == 1 ? "text" : "thinking", value(&b->text));
      if (b->kind == 2) cJSON_AddStringToObject(v, "signature", value(&b->signature));
      if (b->kind == 1 && b->text.size) any = true;
    }
  }
  if (!rc && !any) { s->error = s->limited ? "LLM output token limit reached before answer" : "LLM returned empty text response"; rc = -EPROTO; }
  if (!rc) { *body = cJSON_PrintUnformatted(root); if (!*body) rc = -ENOMEM; }
  cJSON_Delete(root); return rc;
}
const char *claw_stream_error(const struct claw_stream *s) { return s->error ? s->error : "Invalid LLM stream completion"; }
void claw_stream_free(struct claw_stream *s)
{
  if (!s) return;
  free(s->line.data); free(s->event.data); free(s->text.data); free(s->reasoning.data);
  for (unsigned i = 0; i < BLOCKS; i++) { struct block *b = &s->blocks[i]; free(b->id.data); free(b->name.data); free(b->args.data); free(b->text.data); free(b->signature.data); }
  free(s);
}
