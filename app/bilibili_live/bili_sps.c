/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_sps.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
struct bits { uint8_t p[4096]; size_t n, pos; bool error; };
static uint32_t bits(struct bits *b, unsigned n)
{
  uint32_t v = 0;
  if (n > 32 || b->pos + n > b->n * 8) { b->error = true; return 0; }
  while (n--) { v = (v << 1) | ((b->p[b->pos / 8] >> (7 - b->pos % 8)) & 1); b->pos++; }
  return v;
}
static uint32_t ue(struct bits *b)
{
  unsigned zeros = 0;
  while (!b->error && !bits(b, 1)) {
    if (++zeros > 24) { b->error = true; return 0; }
  }
  return ((1u << zeros) - 1) + bits(b, zeros);
}
static int se(struct bits *b)
{
  uint32_t v = ue(b);
  return (v & 1) ? (int)((v + 1) / 2) : -(int)(v / 2);
}
int bili_sps_dimensions(const uint8_t *nal, size_t n, int *w, int *h)
{
  struct bits b = {{0}, 0, 0, false};
  unsigned profile, chroma = 1, poc, frame, separate = 0, refs;
  uint32_t mw, mh, left = 0, right = 0, top = 0, bottom = 0;
  size_t i;
  int zeros = 0;
  uint64_t width, height, cropx, cropy;
  if (n < 4 || n > sizeof(b.p) || (nal[0] & 31) != 7) return -EPROTO;
  for (i = 1; i < n; i++) {
    if (zeros >= 2 && nal[i] == 3) {
      if (i + 1 >= n || nal[i + 1] > 3) return -EPROTO;
      zeros = 0; continue;
    }
    b.p[b.n++] = nal[i]; zeros = nal[i] ? 0 : zeros + 1;
  }
  profile = bits(&b, 8); bits(&b, 16); ue(&b);
  if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
      profile == 44 || profile == 83 || profile == 86 || profile == 118 ||
      profile == 128 || profile == 138 || profile == 139 || profile == 134 || profile == 135) {
    chroma = ue(&b);
    if (chroma > 3) return -EPROTO;
    if (chroma == 3) separate = bits(&b, 1);
    if (ue(&b) || ue(&b)) return -ENOTSUP; /* 8-bit only. */
    bits(&b, 1);
    if (bits(&b, 1)) {
      unsigned count = chroma != 3 ? 8 : 12, j;
      for (j = 0; j < count; j++) if (bits(&b, 1)) {
        int last = 8, next = 8;
        unsigned k, len = j < 6 ? 16 : 64;
        for (k = 0; k < len; k++) {
          if (next) next = (last + se(&b) + 256) & 255;
          last = next ? next : last;
        }
      }
    }
  }
  if (chroma != 1 || separate) return -ENOTSUP; /* OpenH264 output is I420. */
  if (ue(&b) > 12) return -EPROTO;
  poc = ue(&b);
  if (poc == 0) { if (ue(&b) > 12) return -EPROTO; }
  else if (poc == 1) {
    unsigned count;
    bits(&b, 1); se(&b); se(&b); count = ue(&b);
    if (count > 255) return -EPROTO;
    while (count--) se(&b);
  } else if (poc != 2) return -EPROTO;
  /* Bound decoder picture-buffer pressure on the 32 MiB board. */
  refs = ue(&b);
  if (refs > 16) return -EFBIG;
  bits(&b, 1); mw = ue(&b); mh = ue(&b); frame = bits(&b, 1);
  if (!frame) return -ENOTSUP; /* Progressive live video only. */
  bits(&b, 1);
  if (bits(&b, 1)) { left = ue(&b); right = ue(&b); top = ue(&b); bottom = ue(&b); }
  if (b.error) return -EPROTO;
  width = ((uint64_t)mw + 1) * 16; height = ((uint64_t)mh + 1) * 16;
  cropx = ((uint64_t)left + right) * 2;
  cropy = ((uint64_t)top + bottom) * 2;
  if (width > 1280 || height > 720) return -EFBIG;
  if (width * height * (refs + 2) * 3 / 2 > 8 * 1024 * 1024) return -EFBIG;
  if (cropx >= width || cropy >= height) return -EPROTO;
  *w = (int)(width - cropx); *h = (int)(height - cropy);
  return 0;
}
