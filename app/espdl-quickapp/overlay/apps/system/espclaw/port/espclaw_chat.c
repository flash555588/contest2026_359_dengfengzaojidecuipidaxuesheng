/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_chat_backend.h"
#include "claw_core.h"
#include "claw_qpk.h"
#include "claw_tls.h"
#include "llm/backends/claw_llm_backend_openai_compatible.h"
#include "llm/backends/claw_llm_backend_anthropic.h"
#include "freertos/task.h"
#include "chat_root_ca.inc"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_once_t tls_once = PTHREAD_ONCE_INIT;
static int tls_result;
/* These stay alive if the core's bounded stop times out. */
static claw_core_handle_t retained;
static char *retained_context;
static struct claw_qpk_session qpk_session;

static void initialize_tls(void)
{
  tls_result = claw_tls_initialize(chat_root_ca, sizeof(chat_root_ca));
  if (tls_result == -EALREADY) tls_result = 0;
}
int glass_chat_tls_initialize(void)
{ pthread_once(&tls_once, initialize_tls); return tls_result; }

static bool setting(const char *value, size_t limit)
{ return value && value[0] && glass_chat_utf8(value, limit, false); }

int glass_chat_load_settings(struct portal_ai_config *out)
{
  memset(out, 0, sizeof(*out));
  int ret = portal_ai_load(out);
  if (ret == -ENOENT) {
    const char *key = getenv("ESPCLAW_API_KEY"), *base = getenv("ESPCLAW_BASE_URL");
    const char *model = getenv("ESPCLAW_MODEL"), *backend = getenv("ESPCLAW_BACKEND");
    if (!setting(key, 2048) || !setting(base, 1024) || !setting(model, 128) || !setting(backend, 32)) return ret;
    strcpy(out->api_key, key); strcpy(out->base_url, base); strcpy(out->model, model); strcpy(out->backend, backend);
    strcpy(out->system_prompt, "请准确、简洁地回答用户，使用用户的语言。");
    out->timeout_ms = 300000; out->max_tokens = 16384;
  } else if (ret) return ret;
  if (!setting(out->api_key, 2048) || !setting(out->base_url, 1024) || !setting(out->model, 128) ||
      !glass_chat_utf8(out->system_prompt, 1024, true) ||
      (strcmp(out->backend, CLAW_LLM_BACKEND_OPENAI_COMPATIBLE_ID) &&
       strcmp(out->backend, CLAW_LLM_BACKEND_ANTHROPIC_ID)) ||
      strncmp(out->base_url, "https://", 8) || !out->base_url[8] || out->base_url[8] == '/' ||
      strpbrk(out->base_url + 8, "@?#\\ \t\r\n") ||
      out->timeout_ms < 5000 || out->timeout_ms > 3600000 || !out->max_tokens || out->max_tokens > 384000) return -EINVAL;
  return 0;
}

static esp_err_t collect_history(const claw_core_request_t *request,
                                 claw_core_context_t *context, void *user)
{
  (void)request; (void)user;
  context->kind = CLAW_CORE_CONTEXT_KIND_MESSAGES;
  context->content = strdup(retained_context ? retained_context : "[]");
  return context->content ? ESP_OK : ESP_ERR_NO_MEM;
}

enum chat_error glass_chat_classify(int error, const char *detail)
{
  if (error == ESP_ERR_TIMEOUT) return CHAT_ERROR_TIMEOUT;
  if (error == ESP_ERR_NO_MEM) return CHAT_ERROR_MEMORY;
  if (detail) {
    if (strstr(detail, "output token limit")) return CHAT_ERROR_OUTPUT_LIMIT;
    if (strstr(detail, "stream interrupted")) return CHAT_ERROR_STREAM;
    if (strstr(detail, "Unknown LLM backend") || strstr(detail, "is empty")) return CHAT_ERROR_CONFIG;
    if (strstr(detail, "timed out")) return CHAT_ERROR_TIMEOUT;
    if (strstr(detail, "HTTP 401") || strstr(detail, "HTTP 403")) return CHAT_ERROR_AUTH;
    if (strstr(detail, "HTTP 400") || strstr(detail, "HTTP 404")) return CHAT_ERROR_MODEL;
    if (strstr(detail, "HTTP 429")) return CHAT_ERROR_LIMIT;
    if (strstr(detail, "certificate") || strstr(detail, "TLS handshake")) return CHAT_ERROR_TLS;
    if (strstr(detail, "parse") || strstr(detail, "missing message") || strstr(detail, "empty text")) return CHAT_ERROR_RESPONSE;
  }
  return error == ESP_ERR_INVALID_RESPONSE ? CHAT_ERROR_RESPONSE : CHAT_ERROR_NETWORK;
}

static void stream_update(const struct claw_stream_event *event, void *user)
{
  glass_chat_stream((uint32_t)(uintptr_t)user, event);
}
static esp_err_t stream_start(const claw_core_request_t *request, void *user)
{
  (void)user;
  claw_stream_bind(stream_update, (void *)(uintptr_t)request->request_id);
  return ESP_OK;
}

static bool cleanup(void)
{
  if (retained && claw_core_destroy(retained) != ESP_OK) return false;
  retained = NULL; free(retained_context); retained_context = NULL; return true;
}

enum chat_error glass_chat_request(const struct portal_ai_config *settings,
                                  const char *context, const char *prompt,
                                  uint32_t id, atomic_bool *cancel, char **reply)
{
  *reply = NULL;
  if (!cleanup()) return CHAT_ERROR_CLEANUP;
  if (atomic_load(cancel)) return CHAT_ERROR_NONE;
  if (glass_chat_tls_initialize()) return CHAT_ERROR_TLS;
  retained_context = strdup(context);
  if (!retained_context) return CHAT_ERROR_MEMORY;
  claw_core_config_t config = {
    .instance_id = 2, .api_key = settings->api_key, .backend_type = settings->backend,
    .model = settings->model, .base_url = settings->base_url, .system_prompt = settings->system_prompt,
    .timeout_ms = settings->timeout_ms < 300000 ? 300000 : settings->timeout_ms, .max_tokens = settings->max_tokens,
    .on_request_start = stream_start,
    .task_stack_size = 32768, .task_core = tskNO_AFFINITY,
    .request_queue_len = 1, .response_queue_len = 1, .max_tool_iterations = 1, .max_context_providers = 1
  };
  qpk_session.cancel = cancel;
  claw_qpk_configure(&config, &qpk_session);
  enum chat_error result = CHAT_ERROR_NETWORK;
  esp_err_t err = claw_core_create(&config, &retained);
  if (err != ESP_OK) { result = glass_chat_classify(err, NULL); goto done; }
  claw_core_context_provider_t provider = {
    .name = "conversation", .collect = collect_history,
    .flags = CLAW_CORE_CONTEXT_PROVIDER_FLAG_REQUEST_START_ONLY
  };
  err = claw_core_add_context_provider(retained, &provider);
  if (err == ESP_OK) err = claw_qpk_attach(retained);
  if (err == ESP_OK) err = claw_core_start(retained);
  if (err != ESP_OK) { result = glass_chat_classify(err, NULL); goto done; }
  if (atomic_load(cancel)) { result = CHAT_ERROR_NONE; goto done; }
  claw_core_request_t request = {
    .request_id = id, .session_id = "touch-chat", .user_text = prompt,
    .source_channel = "display", .source_chat_id = "touch-chat"
  };
  err = claw_core_submit(retained, &request, 1000);
  if (err != ESP_OK) { result = glass_chat_classify(err, NULL); goto done; }
  for (;;) {
    if (atomic_load(cancel)) (void)claw_core_cancel_request(retained, id);
    claw_core_response_t response = {0};
    err = claw_core_receive_for(retained, id, &response, 100);
    if (err == ESP_OK) {
      if (response.status == CLAW_CORE_RESPONSE_STATUS_OK && response.text && response.text[0]) {
        *reply = response.text; response.text = NULL;
        /* Normalize CRLF and bare CR for the touch text renderer. */
        char *to = *reply;
        for (const char *from = *reply; *from; from++) {
          if (*from == '\r') { if (from[1] == '\n') continue; *to++ = '\n'; }
          else *to++ = *from;
        }
        *to = 0; result = CHAT_ERROR_NONE;
      } else result = glass_chat_classify(ESP_FAIL, response.error_message);
      claw_core_response_free(&response); break;
    }
    claw_core_response_free(&response);
    if (err != ESP_ERR_TIMEOUT) { result = glass_chat_classify(err, NULL); break; }
  }
done:
  if (!cleanup()) result = CHAT_ERROR_CLEANUP;
  return result;
}
