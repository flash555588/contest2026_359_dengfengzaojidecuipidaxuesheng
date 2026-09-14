/****************************************************************************
 * apps/ha_panel/ha_sha1.c
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

#include <string.h>

#include "ha_sha1.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void sha1_transform(FAR uint32_t *state, FAR const uint8_t *block)
{
  uint32_t w[80];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t t;
  int i;

  for (i = 0; i < 16; i++)
    {
      w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
             ((uint32_t)block[i * 4 + 1] << 16) |
             ((uint32_t)block[i * 4 + 2] << 8) |
             ((uint32_t)block[i * 4 + 3]);
    }

  for (i = 16; i < 80; i++)
    {
      w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];

  for (i = 0; i < 80; i++)
    {
      if (i < 20)
        {
          t = ((b & c) | ((~b) & d)) + 0x5a827999;
        }
      else if (i < 40)
        {
          t = (b ^ c ^ d) + 0x6ed9eba1;
        }
      else if (i < 60)
        {
          t = ((b & c) | (b & d) | (c & d)) + 0x8f1bbcdc;
        }
      else
        {
          t = (b ^ c ^ d) + 0xca62c1d6;
        }

      t += ROL32(a, 5) + e + w[i];
      e = d;
      d = c;
      c = ROL32(b, 30);
      b = a;
      a = t;
    }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void ha_sha1_init(FAR struct ha_sha1_s *ctx)
{
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xefcdab89;
  ctx->state[2] = 0x98badcfe;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xc3d2e1f0;
  ctx->count = 0;
  memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void ha_sha1_update(FAR struct ha_sha1_s *ctx, FAR const uint8_t *data,
                    size_t len)
{
  size_t have = ctx->count % 64;
  size_t fill;

  while (len > 0)
    {
      fill = 64 - have;
      if (fill > len)
        {
          fill = len;
        }

      memcpy(&ctx->buffer[have], data, fill);
      ctx->count += fill;
      have += fill;
      data += fill;
      len -= fill;

      if (have == 64)
        {
          sha1_transform(ctx->state, ctx->buffer);
          have = 0;
        }
    }
}

void ha_sha1_final(FAR struct ha_sha1_s *ctx, FAR uint8_t digest[20])
{
  uint64_t bits = (uint64_t)ctx->count * 8;
  uint8_t pad = 0x80;
  int i;

  /* Append the 0x80 terminator and zero padding up to 56 mod 64 */

  ha_sha1_update(ctx, &pad, 1);
  pad = 0;
  while ((ctx->count % 64) != 56)
    {
      ha_sha1_update(ctx, &pad, 1);
    }

  for (i = 0; i < 8; i++)
    {
      uint8_t lenbyte = (uint8_t)(bits >> (56 - i * 8));
      ha_sha1_update(ctx, &lenbyte, 1);
    }

  for (i = 0; i < 5; i++)
    {
      digest[i * 4 + 0] = (uint8_t)(ctx->state[i] >> 24);
      digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
      digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
      digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}
