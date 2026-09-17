/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "claw_core.h"
#include <stdatomic.h>

struct claw_qpk_session {
  atomic_bool *cancel;
};

void claw_qpk_configure(claw_core_config_t *config, struct claw_qpk_session *session);
esp_err_t claw_qpk_attach(claw_core_handle_t core);
