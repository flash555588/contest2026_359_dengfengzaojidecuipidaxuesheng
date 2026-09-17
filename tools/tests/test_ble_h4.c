/* SPDX-License-Identifier: Apache-2.0 */
#include "../c6/ble_h4.h"
#include <assert.h>

int main(void)
{
  const uint8_t reset[] = {0x03, 0x0c, 0};
  const uint8_t event[] = {0x0e, 4, 1, 0x03, 0x0c, 0};
  const uint8_t acl[] = {1, 0x20, 2, 0, 0xaa, 0xbb};
  uint8_t output[16];
  memset(output, 0x55, sizeof(output));
  assert(c6_h4_pack(C6_H4_COMMAND, reset, sizeof(reset), output, 3) == -EMSGSIZE);
  assert(output[0] == 0x55);
  assert(c6_h4_pack(C6_H4_COMMAND, reset, sizeof(reset), output, 4) == 4);
  assert(output[0] == 1 && !memcmp(output + 1, reset, sizeof(reset)));
  assert(c6_h4_receive(C6_H4_EVENT, event, sizeof(event)) == 0);
  assert(c6_h4_receive(C6_H4_EVENT, event, sizeof(event) - 1) == -EMSGSIZE);
  assert(c6_h4_receive(C6_H4_ACL, acl, sizeof(acl)) == 0);
  assert(c6_h4_receive(C6_H4_ACL, acl, sizeof(acl) - 1) == -EMSGSIZE);
  assert(c6_h4_receive(C6_H4_COMMAND, reset, sizeof(reset)) == -EPROTONOSUPPORT);
  assert(c6_h4_receive(3, event, sizeof(event)) == -EPROTONOSUPPORT);
  assert(c6_h4_receive(5, event, sizeof(event)) == -EPROTONOSUPPORT);
  assert(c6_h4_receive(C6_H4_EVENT, NULL, 0) == -EINVAL);
  for (size_t i = 0; i < sizeof(reset); i++)
    assert(c6_h4_validate(C6_H4_COMMAND, reset, i) == -EMSGSIZE);
  const uint8_t huge_acl[] = {0, 0, 0xff, 0xff};
  assert(c6_h4_receive(C6_H4_ACL, huge_acl, sizeof(huge_acl)) == -EMSGSIZE);
  return 0;
}
