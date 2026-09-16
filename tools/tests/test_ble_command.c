/* SPDX-License-Identifier: Apache-2.0 */
#include "../c6/ble_command.h"
#include <assert.h>

int main(void)
{
  uint8_t complete[] = {0x0e, 4, 1, 3, 0x0c, 0};
  uint8_t accepted[] = {0x0f, 4, 0, 1, 3, 0x0c};
  uint8_t status;
  assert(!c6_ble_command_result(complete, 6, 0x0c03, &status));
  assert(status == 0);
  assert(c6_ble_command_result(complete, 6, 0x200b, &status) == -EAGAIN);
  assert(status == 0xff);
  assert(c6_ble_command_result(accepted, 6, 0x0c03, &status) == -EAGAIN);
  assert(status == 0);
  accepted[2] = 0x0c;
  assert(c6_ble_command_result(accepted, 6, 0x0c03, &status) == -EIO);
  assert(status == 0x0c);
  complete[5] = 1;
  assert(c6_ble_command_result(complete, 6, 0x0c03, &status) == -EIO);
  assert(status == 1);
  for (size_t n = 0; n < 6; n++)
    assert(c6_ble_command_result(complete, n, 0x0c03, &status) < 0);
  assert(c6_ble_command_result(NULL, 0, 0x0c03, &status) == -EINVAL);
  assert(c6_ble_command_result(complete, 6, 0x0c03, NULL) == -EINVAL);
  return 0;
}
