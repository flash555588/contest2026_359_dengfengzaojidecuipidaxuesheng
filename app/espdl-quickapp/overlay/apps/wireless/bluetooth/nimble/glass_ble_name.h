/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "glass_ble.h"
#include <stddef.h>
#include <string.h>

/* Advertising strings are untrusted, length-delimited UTF-8. Keep complete
 * codepoints, replace malformed bytes and flatten control characters so a
 * nearby transmitter cannot insert extra rows into the device list.
 */
static inline size_t glass_ble_name_copy(char *out, const uint8_t *data,
                                         size_t length)
{
  size_t used = 0;
  for (size_t i = 0; i < length && used < GLASS_BLE_NAME_MAX;)
    {
      uint8_t c = data[i];
      size_t n = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
                 c >= 0xe0 && c <= 0xef ? 3 :
                 c >= 0xf0 && c <= 0xf4 ? 4 : 0;
      bool valid = n && n <= length - i;
      for (size_t j = 1; valid && j < n; j++)
        valid = (data[i + j] & 0xc0) == 0x80;
      if (valid && n >= 3)
        valid = !((c == 0xe0 && data[i + 1] < 0xa0) ||
                  (c == 0xed && data[i + 1] >= 0xa0) ||
                  (c == 0xf0 && data[i + 1] < 0x90) ||
                  (c == 0xf4 && data[i + 1] >= 0x90));
      if (!valid)
        {
          out[used++] = '?';
          i++;
          continue;
        }
      if (n > GLASS_BLE_NAME_MAX - used) break;
      if (n == 1 && (c < 0x20 || c == 0x7f))
        out[used++] = ' ';
      else
        {
          memcpy(out + used, data + i, n);
          used += n;
        }
      i += n;
    }
  while (used && out[used - 1] == ' ') used--;
  out[used] = '\0';
  return used;
}

/* Merge advertisement and scan-response names for the same address. A
 * complete local name (0x09) takes priority over a shortened name (0x08).
 * A later packet without a name must not erase one already discovered.
 */
static inline void glass_ble_update_name(struct glass_ble_device *device,
                                         const uint8_t *data, size_t length)
{
  if (!data) return;
  while (length)
    {
      size_t field = data[0];
      if (!field || field >= length) break;
      if ((data[1] == 0x08 || data[1] == 0x09) && field > 1)
        {
          char name[GLASS_BLE_NAME_MAX + 1] = {0};
          size_t used = glass_ble_name_copy(name, data + 2, field - 1);
          bool complete = data[1] == 0x09;
          if (used && (complete || (!device->name_complete &&
                                   used > strlen(device->name))))
            {
              memcpy(device->name, name, sizeof(device->name));
              device->name_complete = complete;
            }
        }
      data += field + 1;
      length -= field + 1;
    }
}
