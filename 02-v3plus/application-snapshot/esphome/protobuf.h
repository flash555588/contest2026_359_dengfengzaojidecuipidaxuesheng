/* SPDX-License-Identifier: MIT */
#ifndef OPENVELA_ESPHOME_PROTOBUF_H
#define OPENVELA_ESPHOME_PROTOBUF_H
#include <stddef.h>
#include <stdint.h>

struct eh_pb_reader
{
  const uint8_t *data;
  size_t size;
  size_t pos;
};

struct eh_pb_field
{
  uint32_t number;
  unsigned wire;
  uint64_t value;
  const uint8_t *bytes;
  size_t length;
};

struct eh_pb_writer
{
  uint8_t *data;
  size_t capacity;
  size_t size;
  int error;
};

int eh_pb_next(struct eh_pb_reader *reader, struct eh_pb_field *field);
int eh_pb_varint(struct eh_pb_reader *reader, uint64_t *value);
int eh_pb_u32(const struct eh_pb_field *field, uint32_t *value);
int eh_pb_fixed32(const struct eh_pb_field *field, uint32_t *value);
int eh_pb_float(const struct eh_pb_field *field, float *value);
int eh_pb_string(const struct eh_pb_field *field, char *out, size_t size);
void eh_pb_put_uint(struct eh_pb_writer *writer, uint32_t field, uint32_t value);
void eh_pb_put_fixed32(struct eh_pb_writer *writer, uint32_t field,
                      uint32_t value);
void eh_pb_put_float(struct eh_pb_writer *writer, uint32_t field, float value);
void eh_pb_put_string(struct eh_pb_writer *writer, uint32_t field,
                     const char *value);
#endif
