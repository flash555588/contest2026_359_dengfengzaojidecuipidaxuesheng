/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_SCAN_RESULTS_H
#define C6_SCAN_RESULTS_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#define C6_SCAN_LIMIT 10
struct c6_scan_ap
{
  uint8_t ssid[33];
  size_t ssid_length;
  int32_t rssi;
  uint32_t channel;
};

/* SSIDs are byte strings, not necessarily UTF-8 or NUL terminated. */
static inline int c6_scan_ap_set(struct c6_scan_ap *out,
                               const uint8_t *ssid, size_t length,
                               int32_t rssi, uint32_t channel)
{
  if (!out || length > 32 || (length && !ssid)) return -EINVAL;
  memset(out, 0, sizeof(*out));
  if (length) memcpy(out->ssid, ssid, length);
  out->ssid_length = length;
  out->rssi = rssi;
  out->channel = channel;
  return 0;
}

/* Caller owns the returned records. Count is zero on every failure. */
int esp_hosted_rpc_wifi_scan_results(struct c6_scan_ap *records,
                                    size_t capacity, size_t *count);
#endif
