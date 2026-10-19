/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <cJSON.h>
#include "glass_agent_mode.h"
#define AGENT_MCP_SERVERS 3
#define AGENT_TOOL_BYTES 16384
struct glass_agent;
struct glass_agent *glass_agent_create(atomic_bool *cancel,enum glass_agent_mode mode);
const char *glass_agent_profile(struct glass_agent *agent);
void glass_agent_destroy(struct glass_agent *agent);
char *glass_agent_context(struct glass_agent *agent);
char *glass_agent_tools(struct glass_agent *agent,const char *existing);
/* 1 handled (including tool errors), 0 unknown tool, negative allocation error. */
int glass_agent_call(struct glass_agent *agent,const char *name,const cJSON *args,char **out);
/* Shared by the authenticated config API and runtime. */
bool glass_agent_field(const char *key,unsigned *limit,bool *secret,bool *number);
bool glass_agent_value_valid(const char *key,const cJSON *value);
cJSON *glass_agent_json(const char *text);
int portal_agent_load(cJSON **out);
/* Shared immutable CA bundle already linked by the model backend. */
const unsigned char *glass_agent_trust(size_t *size);
/* Injectable bounded transport for native protocol tests. */
struct glass_mcp_http {
  const char *url,*token,*session,*version,*method,*body;
  unsigned timeout_ms;
  atomic_bool *cancel;
  unsigned response_id;
  int status;
  char response_session[129];
  cJSON *result;
};
int glass_mcp_http_perform(struct glass_mcp_http *request);
/* Optional in-process smart home tools for the board server. The desktop build
 * registers them only when the shared Home Assistant service is linked; tools
 * never open a page or touch LVGL. */
struct glass_board_home {
  const char *catalog; /* comma-separated tool objects, no enclosing brackets */
  char *(*call)(const char *tool,const cJSON *args,atomic_bool *cancel);
};
void glass_agent_set_board_home(const struct glass_board_home *home);
/* Same in-process provider contract, independent of the HA build option. */
void glass_agent_set_board_music(const struct glass_board_home *music);
void glass_agent_set_board_assistant(const struct glass_board_home *assistant);
