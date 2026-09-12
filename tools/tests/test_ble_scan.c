/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include "../c6/ble_scan.h"
#include <assert.h>

int main(void)
{
  uint8_t event[] = {
    0x3e, 27, 2, 2,
    0, 0, 1, 2, 3, 4, 5, 6, 5, 4, 9, 'B', 'L', 'E', 0xd8,
    4, 1, 6, 5, 4, 3, 2, 1, 0, 127
  };
  struct c6_ble_advertisement out[2];
  size_t count;
  assert(!c6_ble_scan_decode(event, sizeof(event), out, 2, &count));
  assert(count == 2 && out[0].rssi == -40 && out[1].rssi == 127);
  assert(out[0].name_length == 3 && !strcmp((char *)out[0].name, "BLE"));
  assert(out[0].address[0] == 1 && out[1].event_type == 4);
  struct c6_ble_advertisement saved[2];
  memcpy(saved, out, sizeof(saved));
  assert(c6_ble_scan_decode(event, sizeof(event), out, 1, &count) == -ENOSPC);
  assert(count == 0 && !memcmp(saved, out, sizeof(saved)));
  for (size_t n = 0; n < sizeof(event); n++)
    {
      assert(c6_ble_scan_decode(event, n, out, 2, &count) < 0);
      assert(count == 0 && !memcmp(saved, out, sizeof(saved)));
    }
  event[22] = 255; /* An address byte is not a C string. */
  assert(!c6_ble_scan_decode(event, sizeof(event), out, 2, &count));
  memcpy(saved, out, sizeof(saved));
  event[27] = 31; /* Bad length in the second report must not publish the first. */
  assert(c6_ble_scan_decode(event, sizeof(event), out, 2, &count) < 0);
  assert(count == 0 && !memcmp(saved, out, sizeof(saved)));
  event[27] = 0;
  event[13] = 31; /* Malformed AD field. */
  assert(c6_ble_scan_decode(event, sizeof(event), out, 2, &count) == -EPROTO);
  event[2] = 13;
  assert(c6_ble_scan_decode(event, sizeof(event), out, 2, &count) == -EPROTONOSUPPORT);
  return 0;
}
