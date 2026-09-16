/* Real incremental parser, arbitrary network segmentation and tool atomicity. */
#include "claw_stream.h"
#include <assert.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned texts, thoughts, tools, tokens;
static void observe(const struct claw_stream_event *e, void *user) {
  (void)user;
  if (e->kind == CLAW_STREAM_TEXT) texts++;
  if (e->kind == CLAW_STREAM_REASONING) thoughts++;
  if (e->kind == CLAW_STREAM_TOOL) tools++;
  if (e->kind == CLAW_STREAM_USAGE && e->has_reasoning_tokens) { assert(e->reasoning_tokens == 12); tokens++; }
}
static char *decode(const char *wire, bool anthropic, size_t chunk, bool success) {
  struct claw_stream *s = claw_stream_create(anthropic); assert(s);
  size_t size = strlen(wire); int rc = 0;
  for (size_t i = 0; i < size && !rc; i += chunk) rc = claw_stream_feed(s, wire + i, size - i < chunk ? size - i : chunk);
  char *body = NULL; if (!rc) rc = claw_stream_finish(s, &body);
  if (success && rc) fprintf(stderr, "%s\n", claw_stream_error(s));
  assert((rc == 0) == success); claw_stream_free(s); return body;
}
int main(void) {
  claw_stream_bind(observe, NULL);
  const char *wire = ": heartbeat\r\n\r\ndata: {\"choices\":[{\"delta\":{\"reasoning_content\":\"先检查\"}}]}\r\n\r\n"
    "data: {\"choices\":[{\"delta\":{\"content\":\"你好\"}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"content\":\" world\"},\"finish_reason\":\"stop\"}]}\n\n"
    "data: {\"choices\":[],\"usage\":{\"completion_tokens\":25,\"completion_tokens_details\":{\"reasoning_tokens\":12}}}\n\n"
    "data: [DONE]\n\n";
  for (size_t n = 1; n < strlen(wire); n++) {
    char *body = decode(wire, false, n, true); assert(strstr(body, "你好 world")); free(body);
  }
  assert(texts && thoughts && tokens);
  const char *tool = "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call0\",\"function\":{\"name\":\"qpk_write_draft\",\"arguments\":\"{\\\"source\\\":\"}}]}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"中\\\"}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\n"
    "data: [DONE]\n\n";
  char *body = decode(tool, false, 1, true); assert(strstr(body, "qpk_write_draft")); free(body); assert(tools);
  free(decode("data: {\"choices\":[{\"delta\":{\"content\":\"partial\"}}]}\n\n", false, 3, false));
  free(decode("data: {broken}\n\n", false, 1, false));
  free(decode("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"thinking\"},\"finish_reason\":\"length\"}]}\n\ndata: [DONE]\n\n", false, 2, false));
  const char *anth = "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"usage\":{\"output_tokens\":1}}}\n\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\"}}\n\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"分析\"}}\n\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"signature_delta\",\"signature\":\"signed\"}}\n\n"
    "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
    "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"a\",\"name\":\"qpk_list_examples\",\"input\":{}}}\n\n"
    "data: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},\"usage\":{\"output_tokens\":20}}\n\n"
    "data: {\"type\":\"message_stop\"}\n\n";
  for (size_t n = 1; n < 80; n++) { body = decode(anth, true, n, true); assert(strstr(body, "signed")); assert(strstr(body, "qpk_list_examples")); free(body); }
  puts("PASS: SSE arbitrary byte boundaries, CRLF, UTF-8, reasoning/usage, OpenAI tool fragments, Anthropic signed thinking and tools; incomplete/error/length-only replies rejected");
  return 0;
}
