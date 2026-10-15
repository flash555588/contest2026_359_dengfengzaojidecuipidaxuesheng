/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
/* Stable persisted values: legacy untagged ESPClaw turns are creation. */
enum glass_agent_mode { AGENT_MODE_CREATE, AGENT_MODE_CHAT, AGENT_MODE_ASSISTANT, AGENT_MODE_COUNT };
static inline bool glass_agent_mode_valid(enum glass_agent_mode mode)
{ return mode >= AGENT_MODE_CREATE && mode < AGENT_MODE_COUNT; }
static inline const char *glass_agent_mode_id(enum glass_agent_mode mode)
{ return mode == AGENT_MODE_CHAT ? "chat" : mode == AGENT_MODE_ASSISTANT ? "assistant" : "create"; }
static inline const char *glass_agent_mode_title(enum glass_agent_mode mode)
{ return mode == AGENT_MODE_CHAT ? "聊天模式" : mode == AGENT_MODE_ASSISTANT ? "助手模式" : "创作模式"; }
