/****************************************************************************
 * apps/ha_panel/ha_b64.c
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

#include "ha_b64.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

static const char g_b64_alphabet[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/****************************************************************************
 * Public Functions
 ****************************************************************************/

size_t ha_b64_encoded_len(size_t len)
{
  return 4 * ((len + 2) / 3) + 1;
}

ssize_t ha_b64_encode(FAR const uint8_t *src, size_t len, FAR char *dst,
                      size_t dstlen)
{
  size_t out = 0;
  size_t i;

  if (dstlen < ha_b64_encoded_len(len))
    {
      return -EINVAL;
    }

  for (i = 0; i + 3 <= len; i += 3)
    {
      uint32_t v = ((uint32_t)src[i] << 16) |
                   ((uint32_t)src[i + 1] << 8) |
                   (uint32_t)src[i + 2];

      dst[out++] = g_b64_alphabet[(v >> 18) & 0x3f];
      dst[out++] = g_b64_alphabet[(v >> 12) & 0x3f];
      dst[out++] = g_b64_alphabet[(v >> 6) & 0x3f];
      dst[out++] = g_b64_alphabet[v & 0x3f];
    }

  if (i < len)
    {
      uint32_t v = (uint32_t)src[i] << 16;
      int rem = (int)(len - i);

      if (rem == 2)
        {
          v |= (uint32_t)src[i + 1] << 8;
        }

      dst[out++] = g_b64_alphabet[(v >> 18) & 0x3f];
      dst[out++] = g_b64_alphabet[(v >> 12) & 0x3f];
      dst[out++] = (rem == 2) ? g_b64_alphabet[(v >> 6) & 0x3f] : '=';
      dst[out++] = '=';
    }

  dst[out] = '\0';
  return (ssize_t)out;
}
