/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t music_read_be32(const unsigned char *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

/* Only report a duration declared by the MP3's first frame. Stream byte
 * counts and the current bitrate do not reliably describe VBR duration. */
static inline unsigned music_frame_duration(const unsigned char *frame,
                                           size_t size, unsigned rate)
{
  if (!frame || size < 4 || !rate || frame[0] != 0xff ||
      (frame[1] & 0xe6) != 0xe2) return 0;
  unsigned version = (frame[1] >> 3) & 3;
  if (version == 1) return 0;
  bool mono = (frame[3] >> 6) == 3;
  size_t side = version == 3 ? (mono ? 17 : 32) : (mono ? 9 : 17);
  size_t offset = 4 + side + ((frame[1] & 1) ? 0 : 2);
  uint32_t count = 0;
  if (size >= offset + 12 &&
      (!memcmp(frame + offset, "Xing", 4) || !memcmp(frame + offset, "Info", 4)) &&
      (music_read_be32(frame + offset + 4) & 1))
    count = music_read_be32(frame + offset + 8);
  else if (size >= 54 && !memcmp(frame + 36, "VBRI", 4))
    count = music_read_be32(frame + 50);
  uint64_t duration = (uint64_t)count * (version == 3 ? 1152 : 576) * 1000 / rate;
  return duration <= UINT32_MAX ? (unsigned)duration : 0;
}
