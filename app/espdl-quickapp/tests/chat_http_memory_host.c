/* SPDX-License-Identifier: Apache-2.0 */
#include "../overlay/apps/system/espclaw/port/http_webclient.c"
#include <assert.h>
static unsigned requests;
static bool oversized;
static bool envelope_fixture;
void webclient_set_defaults(struct webclient_context *c) { memset(c, 0, sizeof(*c)); }
void webclient_set_static_body(struct webclient_context *c, const char *body, size_t size) {
  (void)c;assert(size==strlen(body));
  if(envelope_fixture){
    cJSON *root=cJSON_Parse(body);assert(root);
    const cJSON *thinking=cJSON_GetObjectItemCaseSensitive(root,"thinking");
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(thinking,"type")->valuestring,"enabled"));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"stream")));
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root,"messages"))==1);
    cJSON_Delete(root);
  }
}
int webclient_perform(struct webclient_context *c)
{
  requests++; c->http_status = oversized ? 500 : 200;
  if(envelope_fixture){
    char *data="{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}";
    int length=strlen(data);return c->sink_callback(&data,0,length,&length,c->sink_callback_arg);
  }
  char data[8192]; memset(data, 'x', sizeof(data)); char *p = data; int length = sizeof(data);
  if (!oversized) { memcpy(data, "{}", 2); return c->sink_callback(&p, 0, 2, &length, c->sink_callback_arg); }
  for (unsigned n = 0; n < 300; n++) {
    int rc = c->sink_callback(&p, 0, sizeof(data), &length, c->sink_callback_arg);
    if (rc) return rc;
  }
  assert(!"oversized HTTP error body was not bounded"); return 0;
}
int main(void)
{
  struct webclient_tls_ops ops = {0}; tls_ops = &ops;
  claw_llm_http_json_request_t request = {.url="https://example.test", .body="{}", .auth_type="none", .timeout_ms=1000};
  for (unsigned n = 0; n < 100; n++) {
    claw_llm_http_response_t response; char *error = NULL;
    oversized = true;
    assert(claw_llm_http_post_json(&request, &response, &error) != ESP_OK);
    assert(!response.body && error && strstr(error, "device memory limit")); free(error);
    oversized = false;
    assert(claw_llm_http_post_json(&request, &response, &error) == ESP_OK);
    assert(!error && !strcmp(response.body, "{}")); claw_llm_http_response_free(&response);
  }
  char *huge = malloc(JSON_BODY_LIMIT + 2); assert(huge); memset(huge, 'x', JSON_BODY_LIMIT + 1); huge[JSON_BODY_LIMIT + 1] = 0;
  request.body = huge; unsigned before = requests; claw_llm_http_response_t response; char *error = NULL;
  assert(claw_llm_http_post_json(&request, &response, &error) == ESP_ERR_INVALID_SIZE && requests == before);
  assert(error && strstr(error, "device memory limit")); free(error); free(huge);
  oversized=false;envelope_fixture=true;
  request.body="{\"model\":\"mimo-v2.6-flash\",\"thinking\":{\"type\":\"enabled\"},\"messages\":[{\"role\":\"user\",\"content\":\"hello\"}]}";
  assert(claw_llm_http_post_json(&request,&response,&error)==ESP_OK&&!error);
  claw_llm_http_response_free(&response);
  puts("LLM_HTTP_OPTIONS_OK: provider reasoning options are not overridden; streaming envelopes preserved");
  puts("CHAT_HTTP_MEMORY_OK: 100 oversized error bodies, cleanup and successful recovery; oversized outgoing request rejected before transport");
}
