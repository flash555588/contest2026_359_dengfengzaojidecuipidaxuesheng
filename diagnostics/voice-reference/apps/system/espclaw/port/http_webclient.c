/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_webclient.h"
#include "llm/claw_llm_http_transport.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define RESPONSE_LIMIT (128 * 1024)
#define REQUEST_LIMIT (128 * 1024)
#define HEADER_LIMIT 16
static const struct webclient_tls_ops *tls_ops;
static void *tls_context;

int claw_webclient_set_tls(const struct webclient_tls_ops *ops, void *context)
{
    if (!ops || !ops->connect || !ops->send || !ops->recv || !ops->close)
        return -EINVAL;
    tls_ops = ops;
    tls_context = context;
    return 0;
}

struct response_buffer {
    char *data;
    size_t size;
    atomic_bool *abort_flag;
    int64_t deadline_ms;
};

static int monotonic_ms(int64_t *out)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -errno;
    *out = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    return 0;
}

static int request_state(const struct response_buffer *buffer)
{
    int64_t now;
    if (!buffer) return -EINVAL;
    if (buffer->abort_flag && atomic_load(buffer->abort_flag)) return -ECANCELED;
    if (buffer->deadline_ms) {
        int rc = monotonic_ms(&now);
        if (rc) return rc;
        if (now >= buffer->deadline_ms) return -ETIMEDOUT;
    }
    return 0;
}

static int receive_body(char **data, int offset, int end, int *length, void *arg)
{
    struct response_buffer *buffer = arg;
    int state = request_state(buffer);
    if (state) return state;
    if (!data || !*data || !length || offset < 0 || end < offset || end > *length)
        return -EINVAL;
    size_t count = (size_t)(end - offset);
    if (count > RESPONSE_LIMIT - buffer->size) return -EFBIG;
    char *next = realloc(buffer->data, buffer->size + count + 1);
    if (!next) return -ENOMEM;
    buffer->data = next;
    memcpy(next + buffer->size, *data + offset, count);
    buffer->size += count;
    next[buffer->size] = 0;
    return 0;
}

static int receive_header(const char *line, bool truncated, void *arg)
{
    int state = request_state(arg);
    if (state) return state;
    if (!line) return -EINVAL;
    if (truncated) return -EOVERFLOW;
    /* Called before webclient processes Location; never forward auth. */
    if (!strncasecmp(line, "location:", 9)) return -EPERM;
    return 0;
}

static bool clean_value(const char *text)
{
    if (!text) return false;
    for (; *text; text++) {
        if ((unsigned char)*text < 32 || (unsigned char)*text == 127) return false;
    }
    return true;
}

static char *make_header(const char *name, const char *value)
{
    if (!name || !*name || !clean_value(value) || strlen(value) > 2048) return NULL;
    for (const char *p = name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-')) return NULL;
    }
    size_t size = strlen(name) + strlen(value) + 3;
    char *header = malloc(size);
    if (header) snprintf(header, size, "%s: %s", name, value);
    return header;
}

esp_err_t claw_llm_http_post_json(const claw_llm_http_json_request_t *req,
                                 claw_llm_http_response_t *out, char **error)
{
    struct webclient_context *client = NULL;
    struct response_buffer body = {0};
    char *headers[HEADER_LIMIT + 2] = {0};
    size_t count = 0;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (out) memset(out, 0, sizeof(*out));
    if (error) *error = NULL;
    if (!req || !out || !error || !req->url || !req->body) return result;
    int64_t started;
    if (monotonic_ms(&started)) return ESP_FAIL;
    body.deadline_ms = started + (req->timeout_ms ? req->timeout_ms : 30000);
    if (strncmp(req->url, "https://", 8) || !clean_value(req->url) ||
        !req->url[8] || strchr(req->url, '@') || strlen(req->url) > 2048 ||
        strlen(req->body) > REQUEST_LIMIT || req->header_count > HEADER_LIMIT ||
        (req->header_count && !req->headers)) return result;
    if (!tls_ops) return ESP_ERR_NOT_SUPPORTED;
    if (req->abort_flag && atomic_load(req->abort_flag)) return ESP_ERR_INVALID_STATE;
    body.abort_flag = req->abort_flag;
    headers[count++] = make_header("Content-Type", "application/json");
    if (!headers[0]) return ESP_ERR_NO_MEM;
    const char *auth = req->auth_type ? req->auth_type : "bearer";
    if (strcmp(auth, "none")) {
        if (!req->api_key || !clean_value(req->api_key) || strlen(req->api_key) > 2000)
            goto cleanup;
        if (!strcmp(auth, "api-key")) {
            headers[count] = make_header("X-API-Key", req->api_key);
        } else if (!strcmp(auth, "bearer")) {
            char *value = malloc(strlen(req->api_key) + 8);
            if (!value) { result = ESP_ERR_NO_MEM; goto cleanup; }
            sprintf(value, "Bearer %s", req->api_key);
            headers[count] = make_header("Authorization", value);
            free(value);
        } else goto cleanup;
        if (!headers[count]) goto cleanup;
        count++;
    }
    for (size_t i = 0; i < req->header_count; i++) {
        const char *name = req->headers[i].name;
        if (!name || !strcasecmp(name, "Host") || !strcasecmp(name, "Content-Length") ||
            !strcasecmp(name, "Transfer-Encoding") || !strcasecmp(name, "Authorization") ||
            !strcasecmp(name, "X-API-Key") || !strcasecmp(name, "Content-Type")) goto cleanup;
        headers[count] = make_header(name, req->headers[i].value);
        if (!headers[count]) goto cleanup;
        count++;
    }
    client = calloc(1, sizeof(*client));
    if (!client) { result = ESP_ERR_NO_MEM; goto cleanup; }
    webclient_set_defaults(client);
    client->buffer = malloc(8192);
    if (!client->buffer) { result = ESP_ERR_NO_MEM; goto cleanup; }
    client->buflen = 8192;
    client->method = "POST";
    client->url = req->url;
    client->headers = (const char *const *)headers;
    client->nheaders = count;
    client->timeout_sec = req->timeout_ms ? (req->timeout_ms - 1) / 1000 + 1 : 30;
    client->tls_ops = tls_ops;
    client->tls_ctx = tls_context;
    client->sink_callback = receive_body;
    client->sink_callback_arg = &body;
    client->header_callback = receive_header;
    client->header_callback_arg = &body;
    webclient_set_static_body(client, req->body, strlen(req->body));
    int rc = request_state(&body);
    if (!rc) rc = webclient_perform(client);
    int state = request_state(&body);
    if (state) rc = state;
    if (rc) {
        result = rc == -ETIMEDOUT ? ESP_ERR_TIMEOUT :
                 rc == -ECANCELED ? ESP_ERR_INVALID_STATE : ESP_FAIL;
        goto cleanup;
    }
    if (client->http_status != 200) { result = ESP_FAIL; goto cleanup; }
    if (!body.data) { result = ESP_ERR_INVALID_RESPONSE; goto cleanup; }
    out->body = body.data;
    out->status_code = client->http_status;
    body.data = NULL;
    result = ESP_OK;
cleanup:
    if (client) { free(client->buffer); free(client); }
    for (size_t i = 0; i < count; i++) free(headers[i]);
    free(body.data);
    if (result != ESP_OK) *error = strdup("HTTPS request rejected or failed");
    return result;
}

void claw_llm_http_response_free(claw_llm_http_response_t *response)
{
    if (response) { free(response->body); memset(response, 0, sizeof(*response)); }
}
