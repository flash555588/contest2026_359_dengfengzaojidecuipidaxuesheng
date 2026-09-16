/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_qpk.h"
#include "claw_stream.h"
#include "glass_qpk_builder.h"
#include <cJSON.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static esp_err_t guide_context(const claw_core_request_t *request, claw_core_context_t *out, void *user)
{
  (void)request; (void)user;
  out->kind = CLAW_CORE_CONTEXT_KIND_SYSTEM_PROMPT;
  out->content = strdup(glass_qpk_guide());
  return out->content ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t tools_context(const claw_core_request_t *request, claw_core_context_t *out, void *user)
{
  (void)request; (void)user;
  out->kind = CLAW_CORE_CONTEXT_KIND_TOOLS;
  out->content = strdup(glass_qpk_tools());
  return out->content ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t fail(char **out, const char *kind, const char *detail)
{
  cJSON *root = cJSON_CreateObject();
  if (!root) return ESP_ERR_NO_MEM;
  cJSON_AddBoolToObject(root, "ok", false);
  cJSON_AddStringToObject(root, "error", kind); cJSON_AddStringToObject(root, "detail", detail);
  *out = cJSON_PrintUnformatted(root); cJSON_Delete(root);
  return *out ? ESP_ERR_INVALID_ARG : ESP_ERR_NO_MEM;
}

static bool unsigned_arg(cJSON *root, const char *key, unsigned fallback, unsigned *out)
{
  cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!v) { *out = fallback; return true; }
  if (!cJSON_IsNumber(v) || !(v->valuedouble >= 1 && v->valuedouble <= INT_MAX) ||
      v->valuedouble != (unsigned)v->valuedouble) return false;
  *out = (unsigned)v->valuedouble; return true;
}

static esp_err_t call_tool(const char *name, const char *input, const claw_core_request_t *request,
                           char **out, void *user)
{
  (void)request;
  *out = NULL;
  struct claw_qpk_session *session = user;
  if (!session || !name || !input) return fail(out, "invalid_arguments", "Missing tool session or arguments.");
  if (session->cancel && atomic_load(session->cancel)) return fail(out, "cancelled", "The user cancelled this request.");
  claw_stream_emit(CLAW_STREAM_TOOL, name);
  cJSON *args = strstr(input, "\\u0000") ? NULL : cJSON_ParseWithOpts(input, NULL, true);
  if (!cJSON_IsObject(args)) { cJSON_Delete(args); return fail(out, "invalid_arguments", "Arguments must be one complete JSON object."); }
  if (!strcmp(name, "qpk_list_examples") && cJSON_GetArraySize(args) == 0) *out = glass_qpk_list_examples();
  else if (!strcmp(name, "qpk_read_draft") && cJSON_GetArraySize(args) == 0) *out = glass_qpk_read_draft();
  else if (!strcmp(name, "qpk_write_draft")) *out = glass_qpk_write_draft(input, session->cancel);
  else if (!strcmp(name, "qpk_read_example")) {
    cJSON *example = cJSON_GetObjectItemCaseSensitive(args, "example"), *file = cJSON_GetObjectItemCaseSensitive(args, "file");
    unsigned start, maximum;
    bool known = true;
    for (cJSON *item = args->child; item; item = item->next)
      if (strcmp(item->string, "example") && strcmp(item->string, "file") && strcmp(item->string, "start_line") && strcmp(item->string, "max_lines")) known = false;
    if (known && cJSON_IsString(example) && cJSON_IsString(file) &&
        unsigned_arg(args, "start_line", 1, &start) && unsigned_arg(args, "max_lines", 100, &maximum))
      *out = glass_qpk_read_example(example->valuestring, file->valuestring, start, maximum);
  }
  cJSON_Delete(args);
  if (!*out) return fail(out, "invalid_arguments", "Use the four documented qpk tools and their exact arguments.");
  cJSON *result = cJSON_Parse(*out);
  bool ok = result && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "ok"));
  cJSON_Delete(result);
  return ok ? ESP_OK : ESP_FAIL;
}

void claw_qpk_configure(claw_core_config_t *config, struct claw_qpk_session *session)
{
  config->supports_tools = true;
  config->call_cap = call_tool; config->cap_user_ctx = session;
  config->max_tool_iterations = 8;
  if (config->max_context_providers < 3) config->max_context_providers = 3;
  /* A complete tool argument needs more space than a short chat answer.
   * This is a request budget; the stored provider configuration is untouched. */
  if (config->max_tokens < 4096) config->max_tokens = 4096;
}

esp_err_t claw_qpk_attach(claw_core_handle_t core)
{
  claw_core_context_provider_t guide = {.name = "qpk-developer-guide", .collect = guide_context,
    .flags = CLAW_CORE_CONTEXT_PROVIDER_FLAG_REQUEST_START_ONLY};
  claw_core_context_provider_t tools = {.name = "qpk-tools", .collect = tools_context,
    .flags = CLAW_CORE_CONTEXT_PROVIDER_FLAG_REQUEST_START_ONLY};
  esp_err_t error = claw_core_add_context_provider(core, &guide);
  if (error == ESP_OK) error = claw_core_add_context_provider(core, &tools);
  return error;
}
