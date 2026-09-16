/* Production config, CLI, core, NuttX webclient and shared mbedTLS transport. */
#include "glass_chat_backend.h"
#include "glass_https.h"
#include "claw_tls.h"
#include "claw_task.h"
#include "claw_posix_task.h"
#include "llm/claw_llm_http_transport.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int espclaw_main(int argc, char **argv);
BaseType_t claw_task_create(const claw_task_config_t *cfg, TaskFunction_t fn, void *arg, TaskHandle_t *out)
{ return claw_posix_task_create(fn, arg, 262144, out) == 0 ? pdPASS : pdFAIL; }
void claw_task_delete(TaskHandle_t task) { vTaskDelete(task); }

static unsigned char *read_ca(const char *path, size_t *size)
{
  FILE *file = fopen(path, "rb"); assert(file);
  assert(!fseek(file, 0, SEEK_END)); long length = ftell(file); assert(length > 0);
  rewind(file); unsigned char *data = calloc(1, length + 1); assert(data);
  assert(fread(data, 1, length, file) == (size_t)length); fclose(file);
  *size = length + 1; return data;
}

static int descriptors(void)
{
  DIR *dir = opendir("/proc/self/fd"); assert(dir); int n = 0;
  while (readdir(dir)) n++;
  closedir(dir); return n;
}

static void send_get(const struct webclient_tls_ops *ops, struct glass_https_request *req,
                     struct webclient_tls_connection *conn, const char *path)
{
  char request[256];
  int len = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", path);
  for (int sent = 0; sent < len;) {
    ssize_t n = ops->send(req, conn, request + sent, len - sent); assert(n > 0); sent += n;
  }
}

static void *cancel_later(void *arg)
{ usleep(150000); atomic_store((atomic_bool *)arg, true); return NULL; }

static void test_transport(const char *port, const char *tls12_port, const char *bad_port,
                           const unsigned char *ca, size_t size, const unsigned char *wrong_ca, size_t wrong_size)
{
  const struct webclient_tls_ops *ops = glass_https_tls_ops();
  struct glass_https_diagnostic diag = {0};
  struct glass_https_request req = {.ca_pem=ca, .ca_size=size, .diagnostic=&diag};
  struct webclient_tls_connection *conn = NULL;
  char response[1024];
  for (int version = 0; version < 2; version++) {
    memset(&diag, 0, sizeof(diag)); req.deadline_ms = glass_https_milliseconds() + 5000;
    assert(!ops->connect(&req, "localhost", version ? tls12_port : port, 5, &conn));
    send_get(ops, &req, conn, "/probe");
    size_t used = 0; ssize_t n;
    while ((n = ops->recv(&req, conn, response + used, sizeof(response) - used - 1)) > 0) used += n;
    assert(n == 0); response[used] = 0; assert(strstr(response, "shared-https-ok"));
    assert(!diag.verify_flags && (version ? diag.session_tickets == 0 : diag.session_tickets >= 1));
    assert(!ops->close(&req, conn)); conn = NULL;
    printf("PASS: TLS 1.%d HTTP response, session tickets=%u\n", version ? 2 : 3, diag.session_tickets);
  }
  req.deadline_ms = glass_https_milliseconds() + 5000;
  assert(ops->connect(&req, "127.0.0.1", port, 5, &conn) == -EACCES && !conn);
  assert(diag.verify_flags && diag.verify_flags != UINT32_MAX);
  req.ca_pem = wrong_ca; req.ca_size = wrong_size;
  assert(ops->connect(&req, "localhost", port, 5, &conn) == -EACCES && !conn);
  req.ca_pem = ca; req.ca_size = size;
  assert(ops->connect(&req, "localhost", bad_port, 5, &conn) == -EPROTO && !conn);
  for (int cancel = 0; cancel < 2; cancel++) {
    atomic_bool abort_flag = false; pthread_t helper;
    req.abort_flag = &abort_flag; req.deadline_ms = glass_https_milliseconds() + 5000;
    assert(!ops->connect(&req, "localhost", port, 5, &conn));
    send_get(ops, &req, conn, "/slow");
    if (cancel) assert(!pthread_create(&helper, NULL, cancel_later, &abort_flag));
    int64_t started = glass_https_milliseconds();
    ssize_t ret = ops->recv(&req, conn, response, sizeof(response));
    if (cancel) {
      assert(!pthread_join(helper, NULL)); assert(ret == -ECANCELED);
      assert(glass_https_milliseconds() - started < 1000);
    } else {
      assert(ret == -ETIMEDOUT);
      assert(glass_https_milliseconds() - started < 5500);
    }
    assert(!ops->close(&req, conn)); conn = NULL;
  }
  puts("PASS: certificate/hostname rejection, protocol error classification, timeout and cancellation");
}

static void save_config(const char *url, const char *backend)
{
  cJSON *patch = cJSON_CreateObject(); assert(patch);
  cJSON_AddStringToObject(patch, "backend", backend);
  cJSON_AddStringToObject(patch, "base_url", url);
  cJSON_AddStringToObject(patch, "api_key", "fixture-key");
  cJSON_AddStringToObject(patch, "model", "fixture-model");
  cJSON_AddStringToObject(patch, "system_prompt", "Answer concisely.");
  cJSON_AddNumberToObject(patch, "timeout_ms", 5000);
  assert(!portal_config_save("ai", patch)); cJSON_Delete(patch);
}

static void test_chat(const char *port)
{
  char base[128]; snprintf(base, sizeof(base), "https://localhost:%s/v1", port);
  cJSON *defaults = portal_config_get("ai");
  assert(!strcmp(cJSON_GetObjectItem(defaults, "backend")->valuestring, "openai_compatible"));
  cJSON_Delete(defaults);
  const char *backends[] = {"openai_compatible", "anthropic_compatible"};
  for (size_t i = 0; i < 2; i++) {
    save_config(base, backends[i]);
    struct portal_ai_config config;
    assert(!glass_chat_load_settings(&config));
    assert(!strcmp(config.backend, backends[i]));
    atomic_bool cancel = false; char *reply = NULL;
    assert(glass_chat_request(&config, "[]", "你好 hi", 1, &cancel, &reply) == CHAT_ERROR_NONE);
    assert(reply && !strcmp(reply, "你好，HTTPS 已连接。\nOK")); free(reply);
    char *argv[] = {"espclaw", "ask", "hi", NULL};
    assert(!espclaw_main(3, argv));
  }
  const char *invalid[] = {"openai", "anthropic", "unknown"};
  for (size_t i = 0; i < 3; i++) {
    cJSON *patch = cJSON_CreateObject(); cJSON_AddStringToObject(patch, "backend", invalid[i]);
    assert(portal_config_save("ai", patch) == -EINVAL); cJSON_Delete(patch);
  }
  puts("PASS: production config -> CLI/touch bridge -> both real backends -> real HTTPS; aliases rejected");
}

static void test_http_errors(const char *port)
{
  const char *cases[] = {"401", "429", "redirect", "gzip", "oversize", "chunked"};
  for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
    char url[128]; snprintf(url, sizeof(url), "https://localhost:%s/%s", port, cases[i]);
    claw_llm_http_json_request_t req = {.url=url, .body="{}", .api_key="fixture-key", .timeout_ms=5000};
    claw_llm_http_response_t response = {0}; char *error = NULL;
    esp_err_t ret = claw_llm_http_post_json(&req, &response, &error);
    if (i == 5) assert(ret == ESP_OK && !error && !strcmp(response.body, "{\"ok\":true}"));
    else if (i == 4) assert(ret == ESP_OK && !error && strlen(response.body) == 128 * 1024 + 1);
    else {
      assert(ret != ESP_OK && error && !response.body);
      assert(!strstr(error, "fixture-key") && !strstr(error, "private-provider-body"));
      if (i == 0) assert(strstr(error, "HTTP 401"));
      if (i == 1) assert(strstr(error, "HTTP 429"));
    }
    free(error); claw_llm_http_response_free(&response);
  }
  puts("PASS: HTTP errors, redirects/compression, large responses and chunked responses");
}

static unsigned stream_text_events;
static void stream_progress(const struct claw_stream_event *event, void *user)
{ (void)user; if (event->kind == CLAW_STREAM_TEXT) stream_text_events++; }
static void test_sse(const char *port)
{
  char url[128]; snprintf(url, sizeof(url), "https://localhost:%s/sse", port);
  claw_llm_http_json_request_t req = {.url=url, .body="{\"messages\":[]}", .api_key="fixture-key", .timeout_ms=400};
  claw_llm_http_response_t response = {0}; char *error = NULL;
  claw_stream_bind(stream_progress, NULL);
  int64_t start = glass_https_milliseconds();
  assert(claw_llm_http_post_json(&req, &response, &error) == ESP_OK);
  assert(glass_https_milliseconds() - start > 1000 && stream_text_events == 12);
  assert(response.body && strstr(response.body, "中中中"));
  claw_llm_http_response_free(&response); free(error); error = NULL;
  snprintf(url, sizeof(url), "https://localhost:%s/sse-broken", port);
  assert(claw_llm_http_post_json(&req, &response, &error) != ESP_OK);
  assert(error && strstr(error, "interrupted") && stream_text_events == 13);
  claw_llm_http_response_free(&response); free(error);
  claw_stream_bind(NULL, NULL);
  puts("PASS: real TLS streaming lasts beyond the inactivity timeout while data arrives; partial disconnect rejected after delivering progress");
}

struct concurrent_request { const char *port; const unsigned char *ca; size_t size; };
static void *concurrent_https(void *arg)
{
  const struct concurrent_request *test = arg;
  const struct webclient_tls_ops *ops = glass_https_tls_ops();
  for (unsigned i = 0; i < 20; i++) {
    struct glass_https_request request = {.ca_pem=test->ca, .ca_size=test->size,
                                          .deadline_ms=glass_https_milliseconds()+5000};
    struct webclient_tls_connection *conn = NULL;
    assert(!ops->connect(&request, "localhost", test->port, 5, &conn));
    send_get(ops, &request, conn, "/probe");
    char buffer[1024]; ssize_t n;
    do { n = ops->recv(&request, conn, buffer, sizeof(buffer)); } while (n > 0);
    assert(n == 0); assert(!ops->close(&request, conn));
  }
  return NULL;
}

int main(int argc, char **argv)
{
  assert(argc == 6); setvbuf(stdout, NULL, _IONBF, 0); signal(SIGPIPE, SIG_IGN);
  size_t size, wrong_size;
  unsigned char *ca = read_ca(argv[4], &size), *wrong_ca = read_ca(argv[5], &wrong_size);
  assert(!claw_tls_initialize(ca, size));
  int baseline = descriptors();
  test_transport(argv[1], argv[2], argv[3], ca, size, wrong_ca, wrong_size);
  struct concurrent_request concurrent = {.port=argv[1], .ca=ca, .size=size};
  pthread_t worker; assert(!pthread_create(&worker, NULL, concurrent_https, &concurrent));
  test_chat(argv[1]); test_http_errors(argv[1]); test_sse(argv[1]);
  assert(!pthread_join(worker, NULL));
  assert(descriptors() == baseline);
  free(wrong_ca); free(ca);
  puts("PASS: concurrent HTTPS and chat, 20 reconnects; no socket growth");
  return 0;
}
