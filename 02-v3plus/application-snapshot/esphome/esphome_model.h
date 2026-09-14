/* SPDX-License-Identifier: MIT */
#ifndef OPENVELA_ESPHOME_MODEL_H
#define OPENVELA_ESPHOME_MODEL_H
#include "esphome_client.h"
#include "protobuf.h"
/* Return 1 if handled, 0 for an unsupported entity type, or negative errno.
 * Caller serializes access; rejected packets never publish partial state. */
int eh_model_info(struct esphome_snapshot *model, uint32_t type,
                  const void *body, size_t length);
int eh_model_state(struct esphome_snapshot *model, uint32_t type,
                   const void *body, size_t length);
int eh_model_command(const struct esphome_entity *entity, bool state,
                     bool has_brightness, float brightness,
                     struct eh_pb_writer *writer, uint32_t *type);
#endif
