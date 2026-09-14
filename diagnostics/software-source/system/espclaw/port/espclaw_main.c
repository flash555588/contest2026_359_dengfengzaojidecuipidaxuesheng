/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_core.h"
#include "claw_version.h"
#include "freertos/task.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep ownership if stop times out; the next invocation retries cleanup. */
static pthread_mutex_t command_lock = PTHREAD_MUTEX_INITIALIZER;
static claw_core_handle_t retained_core;

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
    config->api_key = getenv("ESPCLAW_API_KEY");
    config->base_url = getenv("ESPCLAW_BASE_URL");
    config->model = getenv("ESPCLAW_MODEL");
    config->backend_type = getenv("ESPCLAW_BACKEND");
    if (!bounded_setting(config->api_key, 4096) ||
        !bounded_setting(config->base_url, 1024) ||
        !bounded_setting(config->model, 128) ||
        !bounded_setting(config->backend_type, 64)) return false;
    const char *url = config->base_url;
    if (strncmp(url, "https://", 8)) return false;
    const char *authority = url + 8;
    size_t length = strcspn(authority, "/?#");
    if (!length || memchr(authority, '@', length) || strchr(url, '#') ||
        strchr(url, '?') || strchr(url, '\\') || strchr(url, ' ')) return false;
    config->timeout_ms = 30000;
    config->max_tokens = 512;
    config->system_prompt = "Answer the user accurately and concisely.";
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
    err = claw_core_create(&config, &retained_core);
    if (err != ESP_OK) goto done;
    err = claw_core_start(retained_core);
    if (err == ESP_OK) {
        claw_core_request_t request = {
            .request_id = 1, .session_id = ask ? "cli-one-shot" : "offline-selftest",
            .user_text = ask ? argv[2] : "status"
        };
        claw_core_response_t response = {0};
        err = claw_core_submit(retained_core, &request, 1000);
        if (err == ESP_OK) err = claw_core_receive_for(retained_core, 1, &response,
                                                      ask ? 35000 : 3000);
        if (err == ESP_OK && response.request_id == 1 &&
            response.status == CLAW_CORE_RESPONSE_STATUS_OK && response.text &&
            (ask || strstr(response.text, "not configured"))) {
            result = 0;
            if (ask) puts(response.text);
        }
        if (result && err == ESP_OK) err = ESP_FAIL;
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
