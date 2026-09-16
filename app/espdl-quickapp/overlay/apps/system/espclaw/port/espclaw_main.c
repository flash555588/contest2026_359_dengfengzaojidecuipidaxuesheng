/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_core.h"
#include "claw_qpk.h"
#include "claw_version.h"
#include "glass_portal.h"
#include "glass_chat_backend.h"
#include "freertos/task.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep ownership if stop times out; the next invocation retries cleanup. */
static pthread_mutex_t command_lock = PTHREAD_MUTEX_INITIALIZER;
static claw_core_handle_t retained_core;
static struct claw_qpk_session qpk_session;
static bool cli_streamed;
static void cli_stream(const struct claw_stream_event *event, void *user)
{
    (void)user;
    if (event->kind == CLAW_STREAM_TEXT && event->text) { cli_streamed = true; fputs(event->text, stdout); }
    else if (event->kind == CLAW_STREAM_REASONING && event->text) fprintf(stdout, "[thinking] %s", event->text);
    else if (event->kind == CLAW_STREAM_TOOL && event->text) fprintf(stdout, "\n[tool] %s\n", event->text);
    else if (event->kind == CLAW_STREAM_USAGE && event->has_reasoning_tokens) fprintf(stdout, "\n[reasoning_tokens] %lu\n", (unsigned long)event->reasoning_tokens);
    fflush(stdout);
}
static esp_err_t cli_stream_start(const claw_core_request_t *request, void *user)
{ (void)request; (void)user; claw_stream_bind(cli_stream, NULL); return ESP_OK; }

static bool bounded_setting(const char *value, size_t limit)
{
    if (!value || !value[0] || strnlen(value, limit + 1) > limit) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p < 32 || *p == 127) return false;
    }
    return true;
}

static bool chat_config(claw_core_config_t *config)
{
    static struct portal_ai_config saved;
    if (glass_chat_load_settings(&saved)) return false;
    config->api_key = saved.api_key;
    config->base_url = saved.base_url;
    config->model = saved.model;
    config->backend_type = saved.backend;
    config->timeout_ms = saved.timeout_ms;
    config->max_tokens = saved.max_tokens;
    config->system_prompt = saved.system_prompt;
    return true;
}

int espclaw_main(int argc, char **argv)
{
    bool ask = argc == 3 && !strcmp(argv[1], "ask");
    if (!ask && (argc != 2 || (strcmp(argv[1], "selftest") &&
                      strcmp(argv[1], "status") && strcmp(argv[1], "cleanup")))) {
        puts("Usage: espclaw selftest|status|cleanup OR espclaw ask \"question\"");
        return 1;
    }
    if (pthread_mutex_trylock(&command_lock)) {
        fputs("espclaw: another command is running\n", stderr);
        return 1;
    }
    int result = 1;
    esp_err_t err;
    if (!strcmp(argv[1], "status")) {
        claw_core_config_t settings = {0};
        printf("ESP-Claw %s: cleanup pending %s; chat settings %s (network not tested)\n",
               claw_get_version(), retained_core ? "yes" : "no",
               chat_config(&settings) ? "present" : "missing or invalid");
        pthread_mutex_unlock(&command_lock);
        return 0;
    }
    if (!strcmp(argv[1], "cleanup")) {
        err = retained_core ? claw_core_destroy(retained_core) : ESP_OK;
        if (err == ESP_OK) retained_core = NULL;
        printf("espclaw: cleanup %s (code %d)\n",
               retained_core ? "pending" : "complete", err);
        pthread_mutex_unlock(&command_lock);
        return err == ESP_OK ? 0 : 1;
    }
    if (retained_core) {
        err = claw_core_destroy(retained_core);
        if (err != ESP_OK) goto done;
        retained_core = NULL;
    }
    claw_core_config_t config = {
        .system_prompt = "Offline firmware lifecycle check",
        .task_stack_size = 32768,
        .task_core = tskNO_AFFINITY,
        .request_queue_len = 1,
        .response_queue_len = 1,
        .max_tool_iterations = 1,
    };
    if (ask && (!bounded_setting(argv[2], 4096) || !chat_config(&config))) {
        fputs("espclaw: invalid question or missing HTTPS chat settings\n", stderr);
        pthread_mutex_unlock(&command_lock);
        return 1;
    }
    if (ask && glass_chat_tls_initialize()) {
        fputs("espclaw: HTTPS trust initialization failed\n", stderr);
        pthread_mutex_unlock(&command_lock);
        return 1;
    }
    if (ask) {
        cli_streamed = false;
        claw_qpk_configure(&config, &qpk_session);
        config.on_request_start = cli_stream_start;
        if (config.timeout_ms < 300000) config.timeout_ms = 300000;
    }
    err = claw_core_create(&config, &retained_core);
    if (err != ESP_OK) goto done;
    if (ask) err = claw_qpk_attach(retained_core);
    if (err == ESP_OK) err = claw_core_start(retained_core);
    if (err == ESP_OK) {
        claw_core_request_t request = {
            .request_id = 1, .session_id = ask ? "cli-one-shot" : "offline-selftest",
            .user_text = ask ? argv[2] : "status"
        };
        claw_core_response_t response = {0};
        err = claw_core_submit(retained_core, &request, 1000);
        if (err == ESP_OK) err = claw_core_receive_for(retained_core, 1, &response,
                                                      ask ? UINT32_MAX : 3000);
        if (err == ESP_OK && response.request_id == 1 &&
            response.status == CLAW_CORE_RESPONSE_STATUS_OK && response.text &&
            (ask || strstr(response.text, "not configured"))) {
            result = 0;
            if (ask && !cli_streamed) puts(response.text);
            else if (ask) putchar('\n');
        }
        if (result && err == ESP_OK) {
            enum chat_error reason = glass_chat_classify(ESP_FAIL, response.error_message);
            fprintf(stderr, "espclaw: %s\n", glass_chat_error_text(reason));
            err = ESP_FAIL;
        }
        claw_core_response_free(&response);
    }
    esp_err_t cleanup = claw_core_destroy(retained_core);
    if (cleanup == ESP_OK) retained_core = NULL;
    else { result = 1; err = cleanup; }
done:
    if (!result && !ask) printf("ESP-Claw %s: offline core selftest PASS; network/model not tested\n",
                        claw_get_version());
    else if (result) fprintf(stderr, "espclaw: command failed (code %d, cleanup pending %s)\n",
                 err, retained_core ? "yes" : "no");
    pthread_mutex_unlock(&command_lock);
    return result;
}
