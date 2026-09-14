/****************************************************************************
 * apps/ha_panel/ha_ws_frame.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <string.h>

#include "ha_ws_frame.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DECODE_STATE_OP    0
#define DECODE_STATE_LEN   1
#define DECODE_STATE_EXT   2
#define DECODE_STATE_MASK  3

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void ha_ws_decoder_init(FAR struct ha_ws_decoder_s *dec)
{
  memset(dec, 0, sizeof(*dec));
  dec->state = DECODE_STATE_OP;
  dec->hdrneed = 2;
}

int ha_ws_header_feed(FAR struct ha_ws_decoder_s *dec,
                      FAR const uint8_t *data, size_t len,
                      FAR size_t *consumed)
{
  size_t used = 0;

  while (used < len)
    {
      size_t want = dec->hdrneed - dec->hdrlen;
      size_t take = len - used;

      if (take > want)
        {
          take = want;
        }

      memcpy(&dec->hdr[dec->hdrlen], data + used, take);
      dec->hdrlen += take;
      used += take;

      if (dec->hdrlen < dec->hdrneed)
        {
          break;
        }

      switch (dec->state)
        {
          case DECODE_STATE_OP:
            {
              uint8_t b0 = dec->hdr[0];
              uint8_t b1 = dec->hdr[1];

              dec->fin = (b0 & 0x80) != 0;
              dec->opcode = b0 & 0x0f;

              /* RSV bits must be zero; reserved opcodes are errors */

              if ((b0 & 0x70) != 0 || (dec->opcode > HA_WS_OP_PONG) ||
                  (dec->opcode >= 0x3 && dec->opcode <= 0x7))
                {
                  return HA_WS_DECODE_ERROR;
                }

              dec->masked = (b1 & 0x80) != 0;
              dec->payload_len = b1 & 0x7f;
              dec->hdrneed = 2;
              if (dec->payload_len == 126)
                {
                  dec->state = DECODE_STATE_EXT;
                  dec->hdrneed += 2;
                }
              else if (dec->payload_len == 127)
                {
                  dec->state = DECODE_STATE_EXT;
                  dec->hdrneed += 8;
                }
              else
                {
                  dec->state = DECODE_STATE_MASK;
                }

              if (dec->masked)
                {
                  dec->hdrneed += 4;
                }
            }
            break;

          case DECODE_STATE_EXT:
            {
              if (dec->payload_len == 126)
                {
                  dec->payload_len = ((uint64_t)dec->hdr[2] << 8) |
                                     (uint64_t)dec->hdr[3];
                }
              else
                {
                  uint64_t v = 0;
                  int i;

                  for (i = 0; i < 8; i++)
                    {
                      v = (v << 8) | (uint64_t)dec->hdr[2 + i];
                    }

                  /* The most significant bit must be zero (RFC 6455) */

                  if ((v >> 63) != 0)
                    {
                      return HA_WS_DECODE_ERROR;
                    }

                  dec->payload_len = v;
                }

              dec->state = DECODE_STATE_MASK;
            }
            break;

          case DECODE_STATE_MASK:
            {
              if (dec->masked)
                {
                  memcpy(dec->maskkey, &dec->hdr[dec->hdrlen - 4], 4);
                }

              if (consumed != NULL)
                {
                  *consumed = used;
                }

              return HA_WS_DECODE_HEADER;
            }

          default:
            return HA_WS_DECODE_ERROR;
        }
    }

  if (consumed != NULL)
    {
      *consumed = used;
    }

  return HA_WS_DECODE_MORE;
}

void ha_ws_unmask(FAR uint8_t *data, size_t len,
                  FAR const uint8_t maskkey[4], uint64_t offset)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      data[i] ^= maskkey[(offset + i) & 3];
    }
}

int ha_ws_frame_encode(uint8_t opcode, FAR const uint8_t *payload,
                       size_t len, FAR const uint8_t maskkey[4],
                       FAR uint8_t *out, size_t outlen,
                       FAR size_t *outsize)
{
  size_t hdrlen;
  size_t i;

  if (len < 126)
    {
      hdrlen = 2;
    }
  else if (len <= 0xffff)
    {
      hdrlen = 4;
    }
  else
    {
      hdrlen = 10;
    }

  hdrlen += 4;

  if (outlen < hdrlen + len)
    {
      return -EINVAL;
    }

  out[0] = (uint8_t)(0x80 | opcode);

  if (len < 126)
    {
      out[1] = (uint8_t)(0x80 | len);
    }
  else if (len <= 0xffff)
    {
      out[1] = (uint8_t)(0x80 | 126);
      out[2] = (uint8_t)(len >> 8);
      out[3] = (uint8_t)len;
    }
  else
    {
      out[1] = (uint8_t)(0x80 | 127);
      for (i = 0; i < 8; i++)
        {
          out[2 + i] = (uint8_t)((uint64_t)len >> (56 - i * 8));
        }
    }

  memcpy(&out[hdrlen - 4], maskkey, 4);

  for (i = 0; i < len; i++)
    {
      out[hdrlen + i] = payload[i] ^ maskkey[i & 3];
    }

  *outsize = hdrlen + len;
  return 0;
}
