/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_webclient.h"
#include "claw_tls.h"
#include "claw_stream.h"
#include <cJSON.h>
#include "glass_https.h"
#include "llm/claw_llm_http_transport.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

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
    uint32_t idle_timeout_ms;
    struct claw_stream *stream;
    bool sse;
    struct webclient_context *client;
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
    if (count) {
        int64_t now; if (monotonic_ms(&now)) return -EIO;
        buffer->deadline_ms = now + buffer->idle_timeout_ms;
        claw_stream_emit(CLAW_STREAM_ACTIVITY, NULL);
    }
    if (buffer->sse && buffer->stream && buffer->client->http_status == 200)
        return claw_stream_feed(buffer->stream, *data + offset, count);
    if (count > SIZE_MAX - 1 - buffer->size) return -EOVERFLOW;
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
    struct response_buffer *buffer = arg;
    int64_t now; if (monotonic_ms(&now)) return -EIO;
    buffer->deadline_ms = now + buffer->idle_timeout_ms;
    if (!strncasecmp(line, "content-type:", 13) && strstr(line, "text/event-stream")) buffer->sse = true;
    /* Called before webclient processes Location; never forward auth. */
    if (!strncasecmp(line, "location:", 9)) return -EPERM;
    if (!strncasecmp(line, "content-encoding:", 17) && !strstr(line, "identity")) return -ENOTSUP;
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
    if (!name || !*name || !clean_value(value) || strlen(value) > 4096) return NULL;
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
    char *headers[HEADER_LIMIT + 4] = {0};
    struct glass_https_diagnostic diagnostic = {0};
    size_t count = 0;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    char failure[192] = "HTTPS request rejected or failed";
    char *request_body = NULL;
    if (out) memset(out, 0, sizeof(*out));
    if (error) *error = NULL;
    if (!req || !out || !error || !req->url || !req->body) return result;
    int64_t started;
    if (monotonic_ms(&started)) return ESP_FAIL;
    body.idle_timeout_ms = req->timeout_ms ? req->timeout_ms : 300000;
    body.deadline_ms = started + body.idle_timeout_ms;
    if (strncmp(req->url, "https://", 8) || !clean_value(req->url) ||
        !req->url[8] || strchr(req->url, '@') || strlen(req->url) > 2048 ||
        req->header_count > HEADER_LIMIT ||
        (req->header_count && !req->headers)) return result;
    if (!tls_ops) return ESP_ERR_NOT_SUPPORTED;
    if (req->abort_flag && atomic_load(req->abort_flag)) return ESP_ERR_INVALID_STATE;
    /* The port is the streaming adapter for both existing backend envelopes. */
    cJSON *payload = cJSON_ParseWithOpts(req->body, NULL, true);
    bool llm = cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(payload, "messages"));
    bool anthropic = false;
    for (size_t i = 0; i < req->header_count; i++)
        if (req->headers[i].name && !strcasecmp(req->headers[i].name, "anthropic-version")) anthropic = true;
    if (llm) {
        cJSON_DeleteItemFromObjectCaseSensitive(payload, "stream");
        cJSON_AddBoolToObject(payload, "stream", true);
        if (!anthropic) {
            cJSON_DeleteItemFromObjectCaseSensitive(payload, "stream_options");
            cJSON *options = cJSON_AddObjectToObject(payload, "stream_options");
            cJSON_AddBoolToObject(options, "include_usage", true);
        }
        request_body = cJSON_PrintUnformatted(payload);
        body.stream = claw_stream_create(anthropic);
    }
    cJSON_Delete(payload);
    if (llm && (!request_body || !body.stream)) { result = ESP_ERR_NO_MEM; goto cleanup; }
    claw_stream_emit(CLAW_STREAM_CONNECT, NULL);
    body.abort_flag = req->abort_flag;
    headers[count++] = make_header("Content-Type", "application/json");
    if (!headers[0]) { result = ESP_ERR_NO_MEM; goto cleanup; }
    headers[count++] = make_header("Accept", llm ? "text/event-stream, application/json" : "application/json");
    headers[count++] = make_header("Accept-Encoding", "identity");
    if (!headers[1] || !headers[2]) { result = ESP_ERR_NO_MEM; goto cleanup; }
    const char *auth = req->auth_type ? req->auth_type : "bearer";
    if (strcmp(auth, "none")) {
        if (!req->api_key || !clean_value(req->api_key) || strlen(req->api_key) > 2048)
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
    bool api_key_header = false;
    for (size_t i = 0; i < req->header_count; i++) {
        const char *name = req->headers[i].name;
        if (!name || !strcasecmp(name, "Host") || !strcasecmp(name, "Content-Length") ||
            !strcasecmp(name, "Transfer-Encoding") || !strcasecmp(name, "Authorization") ||
            !strcasecmp(name, "Content-Type") ||
            !strcasecmp(name, "Accept-Encoding")) goto cleanup;
        /* The upstream Anthropic backend supplies x-api-key with auth=none. */
        if (!strcasecmp(name, "X-API-Key")) {
            if (strcmp(auth, "none") || api_key_header) goto cleanup;
            api_key_header = true;
        }
        headers[count] = make_header(name, req->headers[i].value);
        if (!headers[count]) goto cleanup;
        count++;
    }
    client = calloc(1, sizeof(*client));
    if (!client) { result = ESP_ERR_NO_MEM; goto cleanup; }
    webclient_set_defaults(client);
    body.client = client;
    client->buffer = malloc(8192);
    if (!client->buffer) { result = ESP_ERR_NO_MEM; goto cleanup; }
    client->buflen = 8192;
    client->method = "POST";
    client->protocol_version = WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;
    client->url = req->url;
    client->headers = (const char *const *)headers;
    client->nheaders = count;
    client->timeout_sec = req->timeout_ms ? (req->timeout_ms - 1) / 1000 + 1 : 30;
    client->tls_ops = tls_ops;
    struct glass_https_request control = {0};
    if (tls_context) control = *(const struct glass_https_request *)tls_context;
    control.abort_flag = req->abort_flag;
    control.deadline_ms = body.deadline_ms;
    control.idle_timeout_ms = body.idle_timeout_ms;
    control.diagnostic = &diagnostic;
    client->tls_ctx = &control;
    client->sink_callback = receive_body;
    client->sink_callback_arg = &body;
    client->header_callback = receive_header;
    client->header_callback_arg = &body;
    const char *wire = request_body ? request_body : req->body;
    webclient_set_static_body(client, wire, strlen(wire));
    int rc = request_state(&body);
    if (!rc) rc = webclient_perform(client);
    int state = request_state(&body);
    if (state) rc = state;
    if (rc) {
        result = rc == -ETIMEDOUT ? ESP_ERR_TIMEOUT :
                 rc == -ECANCELED ? ESP_ERR_INVALID_STATE : ESP_FAIL;
        snprintf(failure, sizeof(failure), "%s (stage=%s, code=%d, tls=%d, verify=%lu)", rc == -EACCES ? "HTTPS certificate verification failed" :
                 rc == -ETIMEDOUT ? "HTTPS request timed out" : rc == -ECANCELED ? "HTTPS request cancelled" :
                 diagnostic.stage && !strcmp(diagnostic.stage,"handshake") ? "HTTPS TLS handshake failed" :
                 "HTTPS connection failed", diagnostic.stage ? diagnostic.stage : "request", rc,
                 diagnostic.tls_error, (unsigned long)diagnostic.verify_flags);
        if (body.sse && body.stream && rc != -ETIMEDOUT && rc != -ECANCELED)
            snprintf(failure, sizeof(failure), "%s (code=%d)", claw_stream_error(body.stream), rc);
        if (rc == -ENOMEM) result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (client->http_status != 200) {
        snprintf(failure, sizeof(failure), "HTTPS HTTP %u", (unsigned)client->http_status);
        result = ESP_FAIL; goto cleanup;
    }
    if (body.sse && body.stream) {
        int decoded = claw_stream_finish(body.stream, &body.data);
        if (decoded) {
            result = decoded == -ENOMEM ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_RESPONSE;
            snprintf(failure, sizeof(failure), "%s", claw_stream_error(body.stream)); goto cleanup;
        }
    }
    if (!body.data) { result = ESP_ERR_INVALID_RESPONSE; goto cleanup; }
    if (llm && !body.sse) {
        cJSON *root = cJSON_ParseWithOpts(body.data, NULL, true);
        cJSON *choice = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "choices"), 0);
        cJSON *finish = cJSON_GetObjectItemCaseSensitive(anthropic ? root : choice, anthropic ? "stop_reason" : "finish_reason");
        bool limited = cJSON_IsString(finish) && (!strcmp(finish->valuestring, "length") || !strcmp(finish->valuestring, "max_tokens"));
        cJSON *message = cJSON_GetObjectItemCaseSensitive(choice, "message");
        cJSON *text = cJSON_GetObjectItemCaseSensitive(message, "content");
        cJSON *reason = cJSON_GetObjectItemCaseSensitive(message, "reasoning_content");
        if (cJSON_IsString(reason)) claw_stream_emit(CLAW_STREAM_REASONING, reason->valuestring);
        if (cJSON_IsString(text)) claw_stream_emit(CLAW_STREAM_TEXT, text->valuestring);
        if (limited) claw_stream_emit(CLAW_STREAM_LIMIT, NULL);
        if (limited && (!cJSON_IsString(text) || !text->valuestring[0] || cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(message, "tool_calls")))) {
            cJSON_Delete(root); result = ESP_ERR_INVALID_RESPONSE;
            snprintf(failure, sizeof(failure), "LLM output token limit reached before complete answer"); goto cleanup;
        }
        /* Log shape only, never content or credentials. */
        fprintf(stderr, "[espclaw] response format=json bytes=%lu parsed=%d limited=%d\n", (unsigned long)body.size, root != NULL, limited);
        cJSON_Delete(root);
    }
    out->body = body.data;
    out->status_code = client->http_status;
    body.data = NULL;
    result = ESP_OK;
cleanup:
    if (client) { free(client->buffer); free(client); }
    for (size_t i = 0; i < count; i++) free(headers[i]);
    free(body.data);
    free(request_body); claw_stream_free(body.stream);
    if (result != ESP_OK) {
        *error = strdup(failure);
        /* Only locally generated stages/codes: never provider bodies or keys. */
        fprintf(stderr,"[espclaw] %s\n",failure);
    }
    return result;
}

void claw_llm_http_response_free(claw_llm_http_response_t *response)
{
    if (response) { free(response->body); memset(response, 0, sizeof(*response)); }
}
