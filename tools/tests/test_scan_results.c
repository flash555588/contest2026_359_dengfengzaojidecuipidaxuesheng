/* SPDX-License-Identifier: Apache-2.0 */
#include "../c6/scan_results.h"
#include <assert.h>

int main(void)
{
  struct c6_scan_ap ap;
  uint8_t name[33];
  memset(name, 'x', sizeof(name));
  assert(c6_scan_ap_set(&ap, name, 32, -42, 6) == 0);
  assert(ap.ssid_length == 32 && ap.ssid[32] == 0);
  assert(ap.rssi == -42 && ap.channel == 6);
  assert(c6_scan_ap_set(&ap, name, 33, 0, 0) == -EINVAL);
  assert(c6_scan_ap_set(&ap, NULL, 1, 0, 0) == -EINVAL);
  assert(c6_scan_ap_set(&ap, NULL, 0, 0, 0) == 0);
  assert(ap.ssid_length == 0 && ap.ssid[0] == 0);
  name[1] = 0;
  assert(c6_scan_ap_set(&ap, name, 3, -80, 11) == 0);
  assert(ap.ssid_length == 3 && ap.ssid[2] == 'x');
  return 0;
}
