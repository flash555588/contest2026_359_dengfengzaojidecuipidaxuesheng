/* SPDX-License-Identifier: MIT */
#include "protobuf.h"

#include <errno.h>
#include <string.h>

int eh_pb_varint(struct eh_pb_reader *r, uint64_t *value)
{
  *value = 0;
  for (unsigned i = 0; i < 10; ++i)
    {
      uint8_t byte;
      if (r->pos >= r->size) return -EPROTO;
      byte = r->data[r->pos++];
      if (i == 9 && byte > 1) return -EPROTO;
      *value |= (uint64_t)(byte & 127) << (7 * i);
      if (!(byte & 128)) return 0;
    }
  return -EPROTO;
}

int eh_pb_next(struct eh_pb_reader *r, struct eh_pb_field *f)
{
  uint64_t tag;
  uint64_t length;
  if (r->pos == r->size) return 0;
  memset(f, 0, sizeof(*f));
  if (eh_pb_varint(r, &tag) < 0 || tag > UINT32_MAX || !(tag >> 3))
    return -EPROTO;
  f->number = (uint32_t)(tag >> 3);
  f->wire = tag & 7;
  if (f->wire == 0)
    return eh_pb_varint(r, &f->value) < 0 ? -EPROTO : 1;
  if (f->wire == 2)
    {
      if (eh_pb_varint(r, &length) < 0) return -EPROTO;
    }
  else if (f->wire == 5) length = 4;
  else if (f->wire == 1) length = 8;
  else return -EPROTO;
  if (length > r->size - r->pos) return -EPROTO;
  f->bytes = r->data + r->pos;
  f->length = (size_t)length;
  r->pos += f->length;
  return 1;
}

int eh_pb_u32(const struct eh_pb_field *f, uint32_t *value)
{
  if (f->wire != 0 || f->value > UINT32_MAX) return -EPROTO;
  *value = (uint32_t)f->value;
  return 0;
}

int eh_pb_fixed32(const struct eh_pb_field *f, uint32_t *value)
{
  if (f->wire != 5) return -EPROTO;
  *value = (uint32_t)f->bytes[0] | ((uint32_t)f->bytes[1] << 8) |
           ((uint32_t)f->bytes[2] << 16) | ((uint32_t)f->bytes[3] << 24);
  return 0;
}

int eh_pb_float(const struct eh_pb_field *f, float *value)
{
  uint32_t bits;
  if (eh_pb_fixed32(f, &bits) < 0) return -EPROTO;
  memcpy(value, &bits, sizeof(bits));
  return 0;
}

int eh_pb_string(const struct eh_pb_field *f, char *out, size_t size)
{
  size_t from = 0;
  size_t to = 0;
  if (f->wire != 2 || !size) return -EPROTO;
  /* Validate UTF-8 and truncate only at character boundaries. Remote labels
   * cannot inject controls or malformed strings into LVGL's text decoder. */
  while (from < f->length)
    {
      uint8_t lead = f->bytes[from];
      size_t count = 1;
      uint32_t cp = lead;
      if (lead >= 0xc2 && lead <= 0xdf) {count = 2; cp &= 31;}
      else if (lead >= 0xe0 && lead <= 0xef) {count = 3; cp &= 15;}
      else if (lead >= 0xf0 && lead <= 0xf4) {count = 4; cp &= 7;}
      else if (lead >= 0x80) return -EPROTO;
      if (count > f->length - from) return -EPROTO;
      for (size_t i = 1; i < count; ++i)
        {
          uint8_t next = f->bytes[from + i];
          if ((next & 0xc0) != 0x80) return -EPROTO;
          cp = (cp << 6) | (next & 63);
        }
      if ((count == 3 && cp < 0x800) || (count == 4 && cp < 0x10000) ||
          (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return -EPROTO;
      if (cp < 32 || (cp >= 127 && cp < 160)) return -EPROTO;
      if (to + count < size)
        {
          memcpy(out + to, f->bytes + from, count);
          to += count;
        }
      else
        {
          /* Keep validating the rest without appending later characters. */
          size = to + 1;
        }
      from += count;
    }
  out[to] = '\0';
  return 0;
}

static void put_byte(struct eh_pb_writer *w, uint8_t byte)
{
  if (w->size == w->capacity) {w->error = -EMSGSIZE; return;}
  if (!w->error) w->data[w->size++] = byte;
}

static void put_varint(struct eh_pb_writer *w, uint32_t value)
{
  do
    {
      uint8_t byte = value & 127;
      value >>= 7;
      put_byte(w, byte | (value ? 128 : 0));
    }
  while (value);
}

void eh_pb_put_uint(struct eh_pb_writer *w, uint32_t field, uint32_t value)
{
  put_varint(w, field << 3);
  put_varint(w, value);
}

void eh_pb_put_fixed32(struct eh_pb_writer *w, uint32_t field, uint32_t value)
{
  put_varint(w, (field << 3) | 5);
  for (unsigned i = 0; i < 4; ++i) put_byte(w, (value >> (8 * i)) & 255);
}

void eh_pb_put_float(struct eh_pb_writer *w, uint32_t field, float value)
{
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  eh_pb_put_fixed32(w, field, bits);
}

void eh_pb_put_string(struct eh_pb_writer *w, uint32_t field, const char *value)
{
  size_t size = strlen(value);
  if (size > UINT32_MAX) {w->error = -EMSGSIZE; return;}
  put_varint(w, (field << 3) | 2);
  put_varint(w, (uint32_t)size);
  for (size_t i = 0; i < size; ++i) put_byte(w, (uint8_t)value[i]);
}
