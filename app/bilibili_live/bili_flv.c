/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_flv.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
static uint32_t be(const uint8_t *p, unsigned n)
{
  uint32_t v = 0;
  while (n--) v = (v << 8) | *p++;
  return v;
}
static int exact(struct bili_flv *f, uint8_t *p, size_t n)
{
  while (n) {
    int r = f->read(f->opaque, p, n);
    if (r <= 0) return r < 0 ? r : -EPIPE;
    if ((size_t)r > n) return -EPROTO;
    p += r; n -= r;
  }
  return 0;
}
static int reserve(uint8_t **p,size_t *capacity,size_t needed,size_t limit)
{
  if(needed>limit)return -EFBIG;
  if(needed<=*capacity)return 0;
  size_t size=(needed+16383u)&~16383u;if(size>limit)size=limit;
  uint8_t *next=realloc(*p,size);if(!next)return -ENOMEM;
  *p=next;*capacity=size;return 0;
}
static int append(struct bili_flv *f, size_t *used,
                  const uint8_t *p, size_t n)
{
  if (!n || n > BILI_AU_LIMIT - 4 || *used > BILI_AU_LIMIT - 4 - n)
    return -EFBIG;
  if(f->managed_buffers){int r=reserve(&f->annexb,&f->annexb_capacity,*used+4+n,BILI_AU_LIMIT);if(r)return r;}
  memcpy(f->annexb + *used, "\0\0\0\1", 4);
  memcpy(f->annexb + *used + 4, p, n);
  *used += n + 4;
  return 0;
}
int bili_avc_config(struct bili_flv *f, const uint8_t *p, size_t n)
{
  size_t pos = 6, used = 0;
  unsigned i, count, group;
  f->configured = 0;
  if (n < 7 || p[0] != 1) return -EPROTO;
  f->nal_length = (p[4] & 3) + 1;
  if (f->nal_length == 3) return -EPROTO;
  count = p[5] & 31;
  if (!count) return -EPROTO;
  for (group = 0; group < 2; group++) {
    for (i = 0; i < count; i++) {
      size_t len;
      if (n - pos < 2) return -EPROTO;
      len = be(p + pos, 2); pos += 2;
      if (!len || len > n - pos ||
          (p[pos] & 31) != (group ? 8 : 7)) return -EPROTO;
      {int r=append(f, &used, p + pos, len);if(r)return r;}
      pos += len;
    }
    if (!group) {
      if (pos >= n) return -EPROTO;
      count = p[pos++];
      if (!count) return -EPROTO;
    }
  }
  /* High-profile AVC extensions may follow PPS. */
  i = f->video(f->opaque, f->annexb, used, 0);
  if ((int)i < 0) return (int)i;
  f->configured = 1;
  return 0;
}
int bili_avc_packet(struct bili_flv *f, const uint8_t *p, size_t n,
                    uint32_t timestamp)
{
  size_t pos = 0, used = 0;
  if (!f->configured) return -EPROTO;
  while (pos < n) {
    size_t len;
    if (n - pos < f->nal_length) return -EPROTO;
    len = be(p + pos, f->nal_length); pos += f->nal_length;
    if (!len || len > n - pos) return -EPROTO;
    {int r=append(f, &used, p + pos, len);if(r)return r;}
    pos += len;
  }
  return used ? f->video(f->opaque, f->annexb, used, timestamp) : -EPROTO;
}
int bili_flv_run(struct bili_flv *f)
{
  uint8_t h[11], previous[4];
  uint32_t offset;
  int r;
  if ((r = exact(f, h, 9))) return r;
  if (memcmp(h, "FLV", 3) || h[3] != 1 || !(h[4] & 1)) return -EPROTO;
  offset = be(h + 5, 4);
  if (offset < 9 || offset > 4096) return -EPROTO;
  while (offset > 9) {
    size_t n = offset - 9 > sizeof(h) ? sizeof(h) : offset - 9;
    if ((r = exact(f, h, n))) return r;
    offset -= n;
  }
  if ((r = exact(f, previous, 4))) return r;
  if (be(previous, 4)) return -EPROTO;
  for (;;) {
    size_t n;
    uint32_t ts;
    if ((r = exact(f, h, 11))) return r;
    n = be(h + 1, 3);
    ts = be(h + 4, 3) | ((uint32_t)h[7] << 24);
    if (n > BILI_TAG_LIMIT) return -EFBIG;
    if (be(h + 8, 3)) return -EPROTO;
    if ((h[0] & 31) == 9 || ((h[0] & 31) == 8 && f->audio)) {
      if(f->managed_buffers&&(r=reserve(&f->tag,&f->tag_capacity,n,BILI_TAG_LIMIT)))return r;
      if ((r = exact(f, f->tag, n))) return r;
    } else {
      uint8_t discard[256];size_t left=n;
      while(left){size_t bytes=left>sizeof(discard)?sizeof(discard):left;
        if((r=exact(f,discard,bytes)))return r;left-=bytes;}
    }
    if ((r = exact(f, previous, 4))) return r;
    if (be(previous, 4) != 11 + n) return -EPROTO;
    if ((h[0] & 31) == 8 && f->audio) {
      if (h[0] & 0xe0) return -ENOTSUP;
      r=f->audio(f->opaque,f->tag,n,ts);if(r<0)return r;
      continue;
    }
    if ((h[0] & 31) != 9) continue; /* AMF metadata / ignored audio. */
    if (h[0] & 0xe0) return -ENOTSUP; /* Encrypted/filtered tag. */
    if (n < 5) return -EPROTO;
    if ((f->tag[0] & 15) != 7) return -ENOTSUP;
    if (f->tag[1] == 0)
      r = bili_avc_config(f, f->tag + 5, n - 5);
    else if (f->tag[1] == 1)
      {
        int32_t composition=(int32_t)(be(f->tag+2,3)<<8)>>8;
        r = bili_avc_packet(f, f->tag + 5, n - 5, ts+(uint32_t)composition);
      }
    else if (f->tag[1] == 2) return 0;
    else return -EPROTO;
    if (r < 0) return r;
  }
}
