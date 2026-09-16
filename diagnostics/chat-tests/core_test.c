/* Real ESPClaw core/backends/queues with a deterministic HTTP peer. */
#include "glass_chat_backend.h"
#include "glass_qpk_builder.h"
#include "claw_qpk.h"
#include "claw_task.h"
#include "claw_posix_task.h"
#include "llm/claw_llm_http_transport.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

void glass_chat_stream(uint32_t id, const struct claw_stream_event *event) { (void)id; (void)event; }

static atomic_int behavior, requests, tls_calls;
static char last_body[131072];
static uint32_t expected_revision;
static const char *fixture_source = "'use strict'; const size=ui.getSize(); ui.text('Fixture',24,24,20,ui.primary);";

static char *tool_response(bool anthropic, const char *name, cJSON *arguments, const char *final)
{
  cJSON *root = cJSON_CreateObject();
  if (anthropic) {
    cJSON_AddStringToObject(root, "role", "assistant");
    cJSON *content = cJSON_AddArrayToObject(root, "content"), *block = cJSON_CreateObject();
    cJSON_AddItemToArray(content, block);
    if (name) {
      cJSON_AddStringToObject(block, "type", "tool_use"); cJSON_AddStringToObject(block, "id", "qpk-call");
      cJSON_AddStringToObject(block, "name", name); cJSON_AddItemToObject(block, "input", arguments);
      cJSON_AddStringToObject(root, "stop_reason", "tool_use");
    } else {
      cJSON_AddStringToObject(block, "type", "text"); cJSON_AddStringToObject(block, "text", final);
      cJSON_AddStringToObject(root, "stop_reason", "end_turn");
    }
  } else {
    cJSON *choices = cJSON_AddArrayToObject(root, "choices"), *choice = cJSON_CreateObject();
    cJSON_AddItemToArray(choices, choice); cJSON *message = cJSON_AddObjectToObject(choice, "message");
    cJSON_AddStringToObject(message, "role", "assistant");
    if (name) {
      cJSON *calls = cJSON_AddArrayToObject(message, "tool_calls"), *call = cJSON_CreateObject();
      cJSON_AddItemToArray(calls, call); cJSON_AddStringToObject(call, "id", "qpk-call");
      cJSON_AddStringToObject(call, "type", "function"); cJSON *function = cJSON_AddObjectToObject(call, "function");
      cJSON_AddStringToObject(function, "name", name);
      char *text = cJSON_PrintUnformatted(arguments); cJSON_Delete(arguments);
      cJSON_AddStringToObject(function, "arguments", text); free(text);
    } else cJSON_AddStringToObject(message, "content", final);
  }
  char *text = cJSON_PrintUnformatted(root); cJSON_Delete(root); return text;
}

/* The host runs as an ordinary user and cannot request NuttX's RR priority. */
BaseType_t claw_task_create(const claw_task_config_t *config, TaskFunction_t fn, void *arg, TaskHandle_t *out)
{ return claw_posix_task_create(fn, arg, config->stack_size < 131072 ? 131072 : config->stack_size, out) == 0 ? pdPASS : pdFAIL; }
void claw_task_delete(TaskHandle_t task) { vTaskDelete(task); }
int claw_tls_initialize(const unsigned char *pem, size_t size)
{
  assert(size > 10000 && pem[size - 1] == 0 && strstr((const char *)pem, "BEGIN CERTIFICATE"));
  atomic_fetch_add(&tls_calls, 1); return 0;
}
int portal_ai_load(struct portal_ai_config *out) { (void)out; return -ENOENT; }

esp_err_t claw_llm_http_post_json(const claw_llm_http_json_request_t *req,
                                 claw_llm_http_response_t *response, char **error)
{
  memset(response, 0, sizeof(*response)); *error = NULL;
  assert(req->url && !strncmp(req->url, "https://unit.invalid/v1/", 24));
  if (strstr(req->url, "/chat/completions")) {
    assert(req->api_key && !strcmp(req->api_key, "host-test-only"));
    assert(!strcmp(req->auth_type, "bearer"));
  } else {
    assert(!strcmp(req->auth_type, "none") && req->header_count == 2);
    assert(!strcmp(req->headers[0].name, "x-api-key"));
    assert(!strcmp(req->headers[0].value, "host-test-only"));
  }
  snprintf(last_body, sizeof(last_body), "%s", req->body);
  int round = atomic_fetch_add(&requests, 1) + 1;
  int test = atomic_load(&behavior);
  if (test == 9 || test == 10 || test == 11) {
    bool anthropic = strstr(req->url, "/messages") != NULL;
    cJSON *body = cJSON_Parse(req->body), *tools = cJSON_GetObjectItem(body, "tools");
    assert(cJSON_GetArraySize(tools) == 4);
    assert(strstr(req->body, "ESPClaw 快应用开发指导") && strstr(req->body, "ui.onTouch(function(state, nx, ny)"));
    cJSON *tokens = cJSON_GetObjectItem(body, "max_tokens");
    assert(tokens && tokens->valueint >= 4096);
    cJSON_Delete(body);
    const char *tool = NULL;
    cJSON *args = cJSON_CreateObject();
    if (test == 9) {
      if (round == 1) tool = "qpk_list_examples";
      else if (round == 2) {
        assert(strstr(req->body, "source_sha256")); tool = "qpk_read_example";
        cJSON_AddStringToObject(args, "example", "hello"); cJSON_AddStringToObject(args, "file", "app.js");
      } else if (round == 3) {
        assert(strstr(req->body, "hello QPK initialized")); tool = "qpk_read_draft";
      } else if (round == 4) tool = "qpk_write_draft";
      else { assert(round == 5 && strstr(req->body, "syntax_checked")); }
    } else if (test == 10) {
      if (round == 1) {
        tool = "qpk_read_example"; cJSON_AddStringToObject(args, "example", "hello");
        cJSON_AddStringToObject(args, "file", "../../config/ai.json");
      } else { assert(round == 2 && strstr(req->body, "not_an_example")); }
    } else {
      if (round <= 2) {
        tool = "qpk_write_draft";
        if (round == 2) assert(strstr(req->body, "syntax_error"));
      } else { assert(round == 3 && strstr(req->body, "syntax_checked")); }
    }
    if (tool && !strcmp(tool, "qpk_write_draft")) {
      cJSON_AddStringToObject(args, "name", "Fixture app"); cJSON_AddStringToObject(args, "slug", "fixture_app");
      cJSON_AddStringToObject(args, "source", test == 11 && round == 1 ? "ui.button(" : fixture_source);
      cJSON_AddNumberToObject(args, "base_revision", expected_revision);
    }
    if (!tool) { cJSON_Delete(args); args = NULL; }
    response->status_code = 200;
    response->body = tool_response(anthropic, tool, args, test == 10 ? "Read refused correctly." : "Draft ready for preview.");
    return ESP_OK;
  }
  if (test == 1) {
    for (unsigned i = 0; i < 3000 && !atomic_load(req->abort_flag); i++) usleep(1000);
    assert(atomic_load(req->abort_flag)); *error = strdup("HTTPS request cancelled"); return ESP_ERR_INVALID_STATE;
  }
  if (test >= 2 && test <= 7) {
    const char *messages[] = {"", "", "HTTPS HTTP 401", "HTTPS HTTP 404", "HTTPS HTTP 429",
                             "HTTPS certificate verification failed", "HTTPS request timed out", "HTTPS HTTP 503"};
    *error = strdup(messages[test]); return test == 6 ? ESP_ERR_TIMEOUT : ESP_FAIL;
  }
  response->status_code = 200;
  if (strstr(req->url, "/chat/completions")) {
    response->body = strdup(test == 8 ? "{\"choices\":[]}" :
      "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"你好，真实核心已解析。\\r\\n下一行\"}}]}");
  } else {
    assert(strstr(req->url, "/messages"));
    response->body = strdup("{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"Anthropic fixture response\"}]}");
  }
  return ESP_OK;
}
void claw_llm_http_response_free(claw_llm_http_response_t *r)
{ free(r->body); memset(r, 0, sizeof(*r)); }

static void *cancel_after_request(void *arg)
{
  for (unsigned i = 0; i < 3000 && !atomic_load(&requests); i++) usleep(1000);
  assert(atomic_load(&requests)); atomic_store((atomic_bool *)arg, true); return NULL;
}

int main(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);
  openlog("espclaw-test", LOG_PERROR, LOG_USER);
  struct portal_ai_config cfg = {0};
  strcpy(cfg.api_key, "host-test-only"); strcpy(cfg.model, "fixture-model");
  strcpy(cfg.base_url, "https://unit.invalid/v1"); strcpy(cfg.backend, "openai_compatible");
  strcpy(cfg.system_prompt, "Answer concisely."); cfg.timeout_ms = 5000; cfg.max_tokens = 128;
  const char *history = "[{\"role\":\"user\",\"content\":\"previous question\"},{\"role\":\"assistant\",\"content\":\"previous answer\"}]";
  atomic_bool cancel = false; char *reply = NULL;
  enum chat_error first = glass_chat_request(&cfg, history, "current question", 1, &cancel, &reply);
  if (first != CHAT_ERROR_NONE) fprintf(stderr, "first request error=%d HTTP calls=%d\n", first, atomic_load(&requests));
  assert(first == CHAT_ERROR_NONE);
  assert(reply && !strcmp(reply, "你好，真实核心已解析。\n下一行")); free(reply);
  cJSON *body = cJSON_Parse(last_body), *messages = cJSON_GetObjectItem(body, "messages");
  assert(cJSON_GetArraySize(messages) == 4);
  assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(messages, 1), "content")->valuestring, "previous question"));
  assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(messages, 2), "role")->valuestring, "assistant"));
  assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(messages, 3), "content")->valuestring, "current question"));
  cJSON_Delete(body);
  enum chat_error expected[] = {CHAT_ERROR_NONE, CHAT_ERROR_NONE, CHAT_ERROR_AUTH, CHAT_ERROR_MODEL,
    CHAT_ERROR_LIMIT, CHAT_ERROR_TLS, CHAT_ERROR_TIMEOUT, CHAT_ERROR_NETWORK, CHAT_ERROR_RESPONSE};
  for (int i = 2; i <= 8; i++) {
    atomic_store(&behavior, i);
    enum chat_error actual = glass_chat_request(&cfg, "[]", "test", i, &cancel, &reply);
    if (actual != expected[i]) fprintf(stderr, "error mapping case=%d got=%d expected=%d\n", i, actual, expected[i]);
    assert(actual == expected[i]); assert(!reply);
  }
  atomic_store(&behavior, 1); atomic_store(&requests, 0);
  pthread_t helper; assert(!pthread_create(&helper, NULL, cancel_after_request, &cancel));
  (void)glass_chat_request(&cfg, "[]", "cancel", 9, &cancel, &reply);
  assert(!pthread_join(helper, NULL)); assert(atomic_load(&cancel)); free(reply);
  atomic_store(&cancel, false); atomic_store(&behavior, 0);
  strcpy(cfg.backend, "anthropic_compatible");
  assert(glass_chat_request(&cfg, history, "current question", 10, &cancel, &reply) == CHAT_ERROR_NONE);
  assert(reply && strstr(reply, "Anthropic fixture")); free(reply);
  assert(atomic_load(&tls_calls) == 1);
  for (unsigned backend = 0; backend < 2; backend++) {
    strcpy(cfg.backend, backend ? "anthropic_compatible" : "openai_compatible");
    for (int scenario = 9; scenario <= 11; scenario++) {
      atomic_store(&behavior, scenario); atomic_store(&requests, 0);
      assert(glass_chat_request(&cfg, "[]", "Create a simple app draft using the verified examples.", 20 + scenario, &cancel, &reply) == CHAT_ERROR_NONE);
      assert(reply && strstr(reply, scenario == 10 ? "Read refused" : "Draft ready")); free(reply);
      if (scenario != 10) expected_revision++;
      struct qpk_draft_info draft; glass_qpk_get(&draft);
      assert(draft.revision == expected_revision && draft.previewed_revision == 0 && draft.saved_revision == 0);
    }
  }
  puts("PASS: both real backends receive the full guide and four tool schemas; read verified qpk files, persist drafts, reject private paths and recover from a syntax error across tool rounds");
  struct claw_qpk_session session = {0};
  claw_core_config_t tool_config = {0}; claw_qpk_configure(&tool_config, &session);
  char *large_source = malloc(192 * 1024 + 1); assert(large_source);
  memset(large_source, ' ', 192 * 1024); large_source[192 * 1024] = 0;
  memcpy(large_source, "ui.text('large draft',20,20);", strlen("ui.text('large draft',20,20);"));
  cJSON *large_args = cJSON_CreateObject();
  cJSON_AddStringToObject(large_args, "name", "Large draft"); cJSON_AddStringToObject(large_args, "slug", "large_draft");
  cJSON_AddStringToObject(large_args, "source", large_source); cJSON_AddNumberToObject(large_args, "base_revision", expected_revision);
  char *large_input = cJSON_PrintUnformatted(large_args); cJSON_Delete(large_args); free(large_source);
  char *tool_output = NULL;
  assert(tool_config.call_cap("qpk_write_draft", large_input, NULL, &tool_output, &session) == ESP_OK);
  assert(tool_output && strstr(tool_output, "syntax_checked")); free(tool_output); free(large_input);
  struct qpk_draft_info after_budget; glass_qpk_get(&after_budget);
  assert(after_budget.revision == expected_revision + 1);
  puts("PASS: 192 KiB tool input compiles and persists through the real tool bridge");
  puts("PASS: real core and OpenAI/Anthropic backends, exact context roles, response decoding, CRLF normalization, HTTP/TLS errors, in-flight cancellation, core teardown/restart");
  return 0;
}
