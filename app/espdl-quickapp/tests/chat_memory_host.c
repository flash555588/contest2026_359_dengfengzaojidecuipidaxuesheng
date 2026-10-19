/* SPDX-License-Identifier: Apache-2.0 */
#include "../overlay/apps/system/espclaw/port/claw_stream.c"
#include <assert.h>
#include <stdio.h>

static int feed(struct claw_stream *s, const char *text)
{ return claw_stream_feed(s, text, strlen(text)); }

static void success(void)
{
  struct claw_stream *s = claw_stream_create(false); assert(s);
  const char *wire = "data: {\"choices\":[{\"delta\":{\"content\":\"hello\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
  for (const char *p = wire; *p; p++) assert(!claw_stream_feed(s, p, 1));
  char *body = NULL; assert(!claw_stream_finish(s, &body));
  assert(strstr(body, "hello")); free(body); claw_stream_free(s);
}

static void anthropic_success(void)
{
  struct claw_stream *s = claw_stream_create(true); assert(s);
  const char *wire =
    "data: {\"type\":\"message_start\",\"message\":{}}\n\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"thinking\",\"thinking\":\"reason\"}}\n\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"signature_delta\",\"signature\":\"signed\"}}\n\n"
    "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
    "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"t1\",\"name\":\"home_status\",\"input\":{}}}\n\n"
    "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{}\"}}\n\n"
    "data: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n\n"
    "data: {\"type\":\"message_stop\"}\n\n";
  for (const char *p = wire; *p; p++) assert(!claw_stream_feed(s, p, 1));
  char *body = NULL; assert(!claw_stream_finish(s, &body));
  assert(strstr(body, "home_status") && strstr(body, "signed") && strstr(body, "reason"));
  free(body); claw_stream_free(s);
}

int main(void)
{
  char *chunk = malloc(16385); assert(chunk); memset(chunk, 'x', 16384); chunk[16384] = 0;
  /* Unterminated framing and multiline events used to grow without bounds. */
  for (unsigned repeat = 0; repeat < 100; repeat++) {
    struct claw_stream *s = claw_stream_create(false); assert(s);
    int rc = 0;
    for (unsigned n = 0; n < 32 && !rc; n++) rc = claw_stream_feed(s, chunk, 16384);
    assert(rc == -EFBIG && s->allocated <= BUFFER_BUDGET);
    claw_stream_free(s);
    s = claw_stream_create(false); assert(s);
    rc = 0;
    for (unsigned n = 0; n < 32 && !rc; n++) {
      assert(!feed(s, "data: "));
      assert(!claw_stream_feed(s, chunk, 16384));
      rc = feed(s, "\n");
    }
    assert(rc == -EFBIG && s->allocated <= BUFFER_BUDGET);
    claw_stream_free(s);
    success(); /* A failed stream must not poison the next request. */
  }
  /* Real fragmented tool arguments are bounded cumulatively across events. */
  struct claw_stream *s = claw_stream_create(false); assert(s);
  cJSON *event = cJSON_Parse("{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{}}]}}]}");
  cJSON *call = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(event, "choices"), 0), "delta"), "tool_calls"), 0);
  cJSON_AddStringToObject(cJSON_GetObjectItem(call, "function"), "arguments", chunk);
  char *json = cJSON_PrintUnformatted(event); assert(json); cJSON_Delete(event);
  int rc = 0;
  for (unsigned n = 0; n < 32 && !rc; n++) {
    assert(!feed(s, "data: ")); assert(!feed(s, json)); rc = feed(s, "\n\n");
  }
  assert(rc == -EFBIG && s->allocated <= BUFFER_BUDGET);
  free(json); claw_stream_free(s);
  /* Many individually legal blocks must still share one allocation budget. */
  s = claw_stream_create(true); assert(s);
  rc = 0;
  for (unsigned n = 0; n < BLOCKS && !rc; n++)
    rc = append(&s->blocks[n].text, chunk, 16384, TEXT_LIMIT);
  assert(!rc);
  while (!rc) for (unsigned n = 0; n < BLOCKS && !rc; n++)
    rc = append(&s->blocks[n].text, chunk, 16384, TEXT_LIMIT);
  assert(rc == -EFBIG && s->allocated <= BUFFER_BUDGET);
  claw_stream_free(s); free(chunk);
  success();
  anthropic_success();
  puts("CHAT_MEMORY_OK: fragmented SSE, cumulative tool/framing/aggregate limits, 100 failures followed by successful requests");
}
