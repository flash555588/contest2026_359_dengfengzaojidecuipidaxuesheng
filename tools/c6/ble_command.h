/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_BLE_COMMAND_H
#define C6_BLE_COMMAND_H
#include "ble_h4.h"

/* Decode status for Reset/Set Scan Parameters/Set Scan Enable diagnostics.
 * Return 0 only on matching Command Complete. EAGAIN means unrelated event
 * or successful Command Status (acceptance is not completion).
 * Caller owns the deadline and must not extend it for unrelated events.
 * No command submission or controller ownership is implemented here.
 */
static inline int c6_ble_command_result(const uint8_t *event, size_t length,
                                         uint16_t opcode, uint8_t *status)
{
  if (!status) return -EINVAL;
  *status = 0xff;
  int ret = c6_h4_receive(C6_H4_EVENT, event, length);
  if (ret < 0) return ret;
  size_t index;
  if (event[0] == 0x0e)
    {
      if (length < 6) return -EMSGSIZE;
      index = 3;
    }
  else if (event[0] == 0x0f)
    {
      if (length != 6) return -EMSGSIZE;
      index = 4;
    }
  else return -EAGAIN;
  uint16_t received = (uint16_t)event[index] |
                      ((uint16_t)event[index + 1] << 8);
  if (received != opcode) return -EAGAIN;
  *status = event[event[0] == 0x0e ? 5 : 2];
  if (*status) return -EIO;
  return event[0] == 0x0e ? 0 : -EAGAIN;
}
#endif
