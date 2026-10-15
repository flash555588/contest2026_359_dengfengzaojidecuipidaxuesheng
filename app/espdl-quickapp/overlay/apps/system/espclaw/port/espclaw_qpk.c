/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_qpk.h"
#include "claw_stream.h"
#include "glass_qpk_builder.h"
#include "glass_agent.h"
#include <stdio.h>
#include <cJSON.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static esp_err_t guide_context(const claw_core_request_t *request, claw_core_context_t *out, void *user)
{
  (void)request; (void)user;
  out->kind = CLAW_CORE_CONTEXT_KIND_SYSTEM_PROMPT;
  struct claw_qpk_session *session=user;
  char *agent=glass_agent_context(session?session->agent:NULL);
  if(!agent)return ESP_ERR_NO_MEM;
  const char *profile=glass_agent_profile(session?session->agent:NULL);
  const char *guide=session&&session->agent&&session->mode==AGENT_MODE_CREATE?glass_qpk_guide():"";
  size_t n=strlen(profile)+strlen(guide)+strlen(agent)+64;
  out->content=malloc(n);if(out->content)snprintf(out->content,n,"%s\n%s\nAgent extensions:\n%s",profile,guide,agent);
  free(agent);
  return out->content ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t tools_context(const claw_core_request_t *request, claw_core_context_t *out, void *user)
{
  (void)request; (void)user;
  out->kind = CLAW_CORE_CONTEXT_KIND_TOOLS;
  struct claw_qpk_session *session=user;
  out->content = glass_agent_tools(session?session->agent:NULL,glass_qpk_tools());
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
  /* Enforce the request's immutable mode even for guessed tool names. */
  if(!session->agent||!glass_agent_mode_valid(session->mode)||session->mode==AGENT_MODE_CHAT||
     (!strncmp(name,"qpk_",4)&&session->mode!=AGENT_MODE_CREATE)||
     (!strncmp(name,"mcp_",4)&&session->mode!=AGENT_MODE_ASSISTANT))
    return fail(out,"mode_denied","Switch mode explicitly before using this tool.");
  claw_stream_emit(CLAW_STREAM_TOOL, name);
  cJSON *args = !strncmp(name,"skill_",6)||!strncmp(name,"mcp_",4)?glass_agent_json(input):
    strstr(input,"\\u0000")?NULL:cJSON_ParseWithOpts(input,NULL,true);
  if (!cJSON_IsObject(args)) { cJSON_Delete(args); return fail(out, "invalid_arguments", "Arguments must be one complete JSON object."); }
  int handled=glass_agent_call(session->agent,name,args,out);
  if(handled){
    cJSON_Delete(args);if(handled<0)return ESP_ERR_NO_MEM;
    cJSON *extension=cJSON_Parse(*out);
    bool failed=!extension||cJSON_GetObjectItemCaseSensitive(extension,"error")||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(extension,"isError"));
    cJSON_Delete(extension);return failed?ESP_FAIL:ESP_OK;
  }
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
  session->agent=glass_agent_create(session->cancel,session->mode);
  config->supports_tools = session->agent && session->mode != AGENT_MODE_CHAT;
  config->call_cap = call_tool; config->cap_user_ctx = session;
  config->max_tool_iterations = 8;
  if (config->max_context_providers < 3) config->max_context_providers = 3;
  /* A complete tool argument needs more space than a short chat answer.
   * This is a request budget; the stored provider configuration is untouched. */
  if (session->mode == AGENT_MODE_CREATE && config->max_tokens < 4096) config->max_tokens = 4096;
}

esp_err_t claw_qpk_attach(claw_core_handle_t core,struct claw_qpk_session *session)
{
  claw_core_context_provider_t guide = {.name = "agent-mode-profile", .collect = guide_context, .user_ctx=session,
    .flags = CLAW_CORE_CONTEXT_PROVIDER_FLAG_REQUEST_START_ONLY};
  claw_core_context_provider_t tools = {.name = "agent-mode-tools", .collect = tools_context, .user_ctx=session,
    .flags = CLAW_CORE_CONTEXT_PROVIDER_FLAG_REQUEST_START_ONLY};
  esp_err_t error = claw_core_add_context_provider(core, &guide);
  if (error == ESP_OK) error = claw_core_add_context_provider(core, &tools);
  return error;
}

void claw_qpk_close(struct claw_qpk_session *session)
{glass_agent_destroy(session->agent);session->agent=NULL;}
