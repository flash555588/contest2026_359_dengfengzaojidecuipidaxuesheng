/****************************************************************************
 * apps/system/c6probe/c6_link_health.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_SYSTEM_C6PROBE_C6_LINK_HEALTH_H
#define __APPS_SYSTEM_C6PROBE_C6_LINK_HEALTH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <stdbool.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Last observed transport health, not proof of Wi-Fi association or BLE
 * service readiness. No bus I/O or locks. -ENETRESET means an uncertain FIFO
 * transfer requires coordinated owner teardown before any companion reset.
 */

int esp_hosted_link_error(void);

/* These errors may occur before consuming FIFO data. The transport converts
 * uncertain CMD53 transfers to -ENETRESET, which must never be retried here.
 */

static inline bool c6_hosted_poll_retryable(int error)
{
  return error == -EIO || error == -ETIMEDOUT || error == -EILSEQ ||
         error == -EINTR || error == -EAGAIN || error == -ENOMEM;
}

#endif /* __APPS_SYSTEM_C6PROBE_C6_LINK_HEALTH_H */
