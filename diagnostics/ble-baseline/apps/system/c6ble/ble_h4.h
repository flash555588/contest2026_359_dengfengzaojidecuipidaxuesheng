/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_BLE_H4_H
#define C6_BLE_H4_H
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define C6_H4_COMMAND 1
#define C6_H4_ACL 2
#define C6_H4_EVENT 4

/* Payload excludes H4 type. Accept exactly one complete HCI packet. */
static inline int c6_h4_validate(uint8_t type, const uint8_t *data,
                                 size_t length)
{
  size_t header;
  size_t payload;
  if (!data) return -EINVAL;
  if (type == C6_H4_COMMAND)
    {
      header = 3;
      if (length < header) return -EMSGSIZE;
      payload = data[2];
    }
  else if (type == C6_H4_EVENT)
    {
      header = 2;
      if (length < header) return -EMSGSIZE;
      payload = data[1];
    }
  else if (type == C6_H4_ACL)
    {
      header = 4;
      if (length < header) return -EMSGSIZE;
      payload = (size_t)data[2] | ((size_t)data[3] << 8);
    }
  else return -EPROTONOSUPPORT;
  return length == header + payload ? 0 : -EMSGSIZE;
}

/* Destination must not overlap source. No output is changed on failure. */
static inline int c6_h4_pack(uint8_t type, const uint8_t *data, size_t length,
                             uint8_t *output, size_t capacity)
{
  if (type != C6_H4_COMMAND && type != C6_H4_ACL)
    return -EPROTONOSUPPORT;
  int ret = c6_h4_validate(type, data, length);
  if (ret < 0) return ret;
  if (!output) return -EINVAL;
  if (capacity <= length) return -EMSGSIZE;
  output[0] = type;
  memcpy(output + 1, data, length);
  return (int)length + 1;
}

static inline int c6_h4_receive(uint8_t type, const uint8_t *data, size_t length)
{
  if (type != C6_H4_EVENT && type != C6_H4_ACL)
    return -EPROTONOSUPPORT;
  return c6_h4_validate(type, data, length);
}
#endif
