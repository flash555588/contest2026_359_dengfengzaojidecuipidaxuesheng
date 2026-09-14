/****************************************************************************
 * apps/system/desktop/qpk_homeassistant.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#pragma once

#include <stdbool.h>
#include <stddef.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct qpk_ha_result_s
{
  bool busy;
  bool done;
  int status;
  int error;
  char *body; /* Transferred once by poll; the caller must free it. */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int qpk_ha_get_state(const char *base_url, const char *token,
                     const char *entity_id);
int qpk_ha_get(const char *base_url, const char *token, const char *resource);
int qpk_ha_call_service(const char *base_url, const char *token,
                        const char *domain, const char *service,
                        const char *data);
void qpk_ha_poll(struct qpk_ha_result_s *result);
/* Nonblocking cancellation: no socket I/O, join or DNS wait on the caller.
 * Poll remains busy and submissions return -EBUSY until the old worker
 * finishes; cancelled results are discarded. The worker owns its socket.
 * DNS may outlast the I/O deadline. A sent service call cannot be undone.
 */
void qpk_ha_stop(void);
