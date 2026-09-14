/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_SETTINGS_SERVICE_H
#define GLASS_SETTINGS_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

struct glass_preferences_status
{
  bool pending;
  int error; /* Result of the latest completed request when pending is false. */
};

/* Copy four settings bytes; no caller-owned buffer survives this call.
 * A successful submission is queued, not yet durable. A worker coalesces
 * changes for 250 ms and preserves the existing two-record storage format.
 */
int glass_preferences_submit(const uint8_t values[4]);
int glass_preferences_get_status(struct glass_preferences_status *status);

/* Saves continue after the settings page closes. There is no persistent
 * worker when idle, and callers must check status rather than join a thread.
 */
#endif
