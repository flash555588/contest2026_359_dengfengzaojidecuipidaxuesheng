/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "glass_chat.h"
#include "glass_portal.h"
#include <stdatomic.h>
#include "claw_stream.h"
void glass_chat_stream(uint32_t id, const struct claw_stream_event *event);

/* One chat worker owns the backend, including any core awaiting cleanup. */
enum chat_error glass_chat_request(const struct portal_ai_config *settings,
                                  const char *context, const char *prompt,
                                  uint32_t id, atomic_bool *cancel, char **reply);
int glass_chat_tls_initialize(void);
int glass_chat_load_settings(struct portal_ai_config *out);
enum chat_error glass_chat_classify(int error, const char *detail);
