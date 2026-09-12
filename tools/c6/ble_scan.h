/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_BLE_SCAN_H
#define C6_BLE_SCAN_H

#include "ble_h4.h"
#include <stdbool.h>

struct c6_ble_advertisement
{
  uint8_t event_type;
  uint8_t address_type;
  uint8_t address[6]; /* Controller byte order; display in reverse order. */
  int16_t rssi;      /* 127 means unavailable, not a measured signal. */
  uint8_t data_length;
  uint8_t data[31];
  uint8_t name_length;
  uint8_t name[32]; /* Raw name bytes, not guaranteed UTF-8. */
};

/* Decode legacy LE Advertising Report (subevent 0x02), payload without H4.
 * Extended advertisements are explicitly unsupported. Validate all reports
 * before writing output; count remains zero on every error.
 */
static inline int c6_ble_scan_decode(const uint8_t *event, size_t length,
                                     struct c6_ble_advertisement *out,
                                     size_t capacity, size_t *count)
{
  if (!count) return -EINVAL;
  *count = 0;
  int ret = c6_h4_receive(C6_H4_EVENT, event, length);
  if (ret < 0) return ret;
  if (event[0] != 0x3e) return -EPROTONOSUPPORT;
  if (length < 4) return -EMSGSIZE;
  if (event[2] != 0x02) return -EPROTONOSUPPORT;
  size_t reports = event[3];
  if (!reports || reports > 25) return -EPROTO;
  if (!out) return -EINVAL;
  size_t offset = 4;
  for (size_t i = 0; i < reports; i++)
    {
      if (length - offset < 10) return -EMSGSIZE;
      const uint8_t *record = event + offset;
      size_t n = record[8];
      if (record[0] > 4 || record[1] > 3 || n > 31) return -EPROTO;
      if (n > length - offset - 10) return -EMSGSIZE;
      size_t position = 0;
      while (position < n)
        {
          size_t field = record[9 + position++];
          if (!field) break;
          if (field > n - position) return -EPROTO;
          position += field;
        }
      offset += 10 + n;
    }
  if (offset != length) return -EPROTO;
  if (reports > capacity) return -ENOSPC;
  offset = 4;
  for (size_t i = 0; i < reports; i++)
    {
      const uint8_t *record = event + offset;
      struct c6_ble_advertisement *result = out + i;
      memset(result, 0, sizeof(*result));
      result->event_type = record[0];
      result->address_type = record[1];
      memcpy(result->address, record + 2, 6);
      size_t n = result->data_length = record[8];
      memcpy(result->data, record + 9, n);
      uint8_t signal = record[9 + n];
      result->rssi = signal <= 127 ? signal : (int16_t)signal - 256;
      bool complete_name = false;
      for (size_t position = 0; position < n; )
        {
          size_t field = result->data[position++];
          if (!field) break;
          uint8_t type = result->data[position];
          if ((type == 8 && !complete_name) || type == 9)
            {
              memset(result->name, 0, sizeof(result->name));
              result->name_length = field - 1;
              memcpy(result->name, result->data + position + 1, field - 1);
              complete_name = type == 9;
            }
          position += field;
        }
      offset += 10 + n;
    }
  *count = reports;
  return 0;
}
#endif
