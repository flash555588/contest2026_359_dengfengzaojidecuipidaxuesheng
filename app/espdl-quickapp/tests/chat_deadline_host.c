/* SPDX-License-Identifier: Apache-2.0 */
#define clock_gettime fake_clock_gettime
#include "../overlay/apps/system/espclaw/port/espclaw_chat.c"
#include <assert.h>
static uint64_t tick;
static unsigned receives, creates, destroys, cancels, closes;
static bool fail_cleanup, cancel_receive, success_receive;
static atomic_bool *user_cancel;
int fake_clock_gettime(clockid_t clock, struct timespec *out)
{ (void)clock; out->tv_sec = tick / 1000; out->tv_nsec = (tick % 1000) * 1000000; return 0; }
int claw_tls_initialize(const unsigned char *pem, size_t size)
{ (void)pem; (void)size; return 0; }
void claw_qpk_configure(claw_core_config_t *config, struct claw_qpk_session *session)
{ assert(config->timeout_ms == 5000); session->agent = (struct glass_agent *)malloc(1); assert(session->agent); }
void claw_qpk_close(struct claw_qpk_session *session)
{ free(session->agent); session->agent = NULL; closes++; }
esp_err_t claw_qpk_attach(claw_core_handle_t core, struct claw_qpk_session *session)
{ (void)core; (void)session; return ESP_OK; }
esp_err_t claw_core_create(const claw_core_config_t *config, claw_core_handle_t *out)
{ (void)config; *out = (claw_core_handle_t)malloc(1); creates++; return ESP_OK; }
esp_err_t claw_core_destroy(claw_core_handle_t core)
{ if (fail_cleanup) return ESP_ERR_TIMEOUT; free(core); destroys++; return ESP_OK; }
esp_err_t claw_core_add_context_provider(claw_core_handle_t core, const claw_core_context_provider_t *p)
{ (void)core; (void)p; return ESP_OK; }
esp_err_t claw_core_start(claw_core_handle_t core)
{ (void)core; return ESP_OK; }
esp_err_t claw_core_submit(claw_core_handle_t core, const claw_core_request_t *r, uint32_t timeout)
{ (void)core; (void)r; (void)timeout; return ESP_OK; }
esp_err_t claw_core_cancel_request(claw_core_handle_t core, uint32_t id)
{ (void)core; (void)id; cancels++; return ESP_OK; }
esp_err_t claw_core_receive_for(claw_core_handle_t core, uint32_t id, claw_core_response_t *r, uint32_t timeout)
{
  (void)core; (void)id; (void)timeout; receives++; tick += 100;
  if (cancel_receive) atomic_store(user_cancel, true);
  if (success_receive) { r->status = CLAW_CORE_RESPONSE_STATUS_OK; r->text = strdup("ok"); return ESP_OK; }
  return ESP_ERR_TIMEOUT;
}
void claw_core_response_free(claw_core_response_t *r) { free(r->text); free(r->error_message); memset(r, 0, sizeof(*r)); }
void claw_stream_bind(claw_stream_observer fn, void *user) { (void)fn; (void)user; }
void glass_chat_stream(uint32_t id, const struct claw_stream_event *event) { (void)id; (void)event; }
int main(void)
{
  struct portal_ai_config settings = {.timeout_ms = 5000}; atomic_bool cancel = false; user_cancel = &cancel;
  for (unsigned n = 0; n < 100; n++) {
    char *reply = NULL; unsigned before = receives;
    assert(glass_chat_request(&settings, "[]", "hello", n + 1, AGENT_MODE_ASSISTANT, &cancel, &reply) == CHAT_ERROR_TIMEOUT);
    assert(!reply && receives - before == 50 && !retained && !retained_context && !qpk_session.agent);
  }
  cancel_receive = true; unsigned before = receives; char *reply = NULL;
  assert(glass_chat_request(&settings, "[]", "hello", 101, AGENT_MODE_ASSISTANT, &cancel, &reply) == CHAT_ERROR_NONE);
  assert(receives == before + 1 && !retained);
  cancel_receive = false; atomic_store(&cancel, false);
  fail_cleanup = true;
  assert(glass_chat_request(&settings, "[]", "hello", 102, AGENT_MODE_ASSISTANT, &cancel, &reply) == CHAT_ERROR_CLEANUP);
  assert(retained && retained_context && qpk_session.agent);
  before = creates;
  assert(glass_chat_request(&settings, "[]", "hello", 103, AGENT_MODE_ASSISTANT, &cancel, &reply) == CHAT_ERROR_CLEANUP && creates == before);
  fail_cleanup = false; success_receive = true;
  assert(glass_chat_request(&settings, "[]", "hello", 104, AGENT_MODE_ASSISTANT, &cancel, &reply) == CHAT_ERROR_NONE);
  assert(reply && !strcmp(reply, "ok") && creates == destroys && cancels == 102 && closes >= destroys);
  free(reply);
  assert(glass_chat_classify(ESP_FAIL, "LLM response exceeds device memory limit") == CHAT_ERROR_MEMORY);
  puts("CHAT_DEADLINE_OK: 100 non-finishing requests, cancellation, bounded cleanup retention, recovery and success");
}
