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

struct hass_transport_result_s
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

int hass_transport_get_state(const char *base_url, const char *token,
                     const char *entity_id);
int hass_transport_get(const char *base_url, const char *token, const char *resource);
int hass_transport_call_service(const char *base_url, const char *token,
                        const char *domain, const char *service,
                        const char *data);
void hass_transport_poll(struct hass_transport_result_s *result);
/* Nonblocking cancellation: no socket I/O, join or DNS wait on the caller.
 * Poll remains busy and submissions return -EBUSY until the old worker
 * finishes; cancelled results are discarded. The worker owns its socket.
 * DNS may outlast the I/O deadline. A sent service call cannot be undone.
 */
void hass_transport_stop(void);
