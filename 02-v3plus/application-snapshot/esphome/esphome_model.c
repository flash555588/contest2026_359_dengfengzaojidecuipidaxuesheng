/* SPDX-License-Identifier: MIT
 * Adapted workflow: aioesphomeapi/client.py and model.py.
 * Wire field constants are generated from the official api.proto.
 */
#include "esphome_model.h"
#include "protocol_ids.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

struct entity_schema
{
  enum esphome_entity_kind kind;
  uint32_t info;
  uint32_t state;
  uint32_t info_device;
  uint32_t state_device;
};

#define SCHEMA(kind, name) \
  { ESPHOME_##kind, EH_ListEntities##name##Response, EH_##name##StateResponse, \
    EH_ListEntities##name##Response_device_id, EH_##name##StateResponse_device_id }

static const struct entity_schema schemas[] =
{
  SCHEMA(SENSOR, Sensor), SCHEMA(BINARY_SENSOR, BinarySensor),
  SCHEMA(SWITCH, Switch), SCHEMA(LIGHT, Light),
  SCHEMA(TEXT_SENSOR, TextSensor)
};

static int color_modes(const struct eh_pb_field *f, bool *supported)
{
  if (f->wire == 0)
    {
      if (f->value > UINT32_MAX) return -EPROTO;
      *supported |= (f->value & 2) != 0;
    }
  else if (f->wire == 2)
    {
      struct eh_pb_reader r = {f->bytes, f->length, 0};
      while (r.pos < r.size)
        {
          uint64_t mode;
          if (eh_pb_varint(&r, &mode) < 0 || mode > UINT32_MAX)
            return -EPROTO;
          *supported |= (mode & 2) != 0;
        }
    }
  else return -EPROTO;
  return 0;
}

int eh_model_info(struct esphome_snapshot *model, uint32_t type,
                  const void *body, size_t length)
{
  const struct entity_schema *schema = NULL;
  struct esphome_entity entity = {0};
  struct eh_pb_reader r = {body, length, 0};
  struct eh_pb_field f;
  char object_id[ESPHOME_NAME_SIZE] = {0};
  bool key_seen = false;
  int ret;
  for (size_t i = 0; i < sizeof(schemas) / sizeof(schemas[0]); ++i)
    if (type == schemas[i].info) schema = &schemas[i];
  if (!schema) return 0;
  entity.kind = schema->kind;
  while ((ret = eh_pb_next(&r, &f)) > 0)
    {
      /* object_id/key/name are common to all five official entity messages. */
      if (f.number == EH_ListEntitiesSensorResponse_key)
        {
          if (eh_pb_fixed32(&f, &entity.key) < 0) return -EPROTO;
          key_seen = true;
        }
      else if (f.number == EH_ListEntitiesSensorResponse_name)
        {
          if (eh_pb_string(&f, entity.name, sizeof(entity.name)) < 0)
            return -EPROTO;
        }
      else if (f.number == EH_ListEntitiesSensorResponse_object_id)
        {
          if (eh_pb_string(&f, object_id, sizeof(object_id)) < 0)
            return -EPROTO;
        }
      else if (f.number == schema->info_device)
        {
          if (eh_pb_u32(&f, &entity.device_id) < 0) return -EPROTO;
        }
      else if (entity.kind == ESPHOME_SENSOR &&
               f.number == EH_ListEntitiesSensorResponse_unit_of_measurement)
        {
          if (eh_pb_string(&f, entity.unit, sizeof(entity.unit)) < 0)
            return -EPROTO;
        }
      else if (entity.kind == ESPHOME_LIGHT &&
               f.number == EH_ListEntitiesLightResponse_supported_color_modes)
        {
          if (color_modes(&f, &entity.supports_brightness) < 0) return -EPROTO;
        }
      else if (entity.kind == ESPHOME_LIGHT &&
               f.number == EH_ListEntitiesLightResponse_legacy_supports_brightness)
        {
          uint32_t value;
          if (eh_pb_u32(&f, &value) < 0) return -EPROTO;
          entity.supports_brightness |= value != 0;
        }
    }
  if (ret < 0 || !key_seen) return -EPROTO;
  if (!entity.name[0])
    {
      if (object_id[0]) memcpy(entity.name, object_id, sizeof(entity.name));
      else snprintf(entity.name, sizeof(entity.name), "Entity %08lx",
                    (unsigned long)entity.key);
    }
  for (size_t i = 0; i < model->count; ++i)
    {
      const struct esphome_entity *other = &model->entities[i];
      if (other->key == entity.key && other->device_id == entity.device_id &&
          other->kind == entity.kind) return -EEXIST;
    }
  if (model->count == ESPHOME_MAX_ENTITIES) model->truncated = true;
  else model->entities[model->count++] = entity;
  ++model->revision;
  return 1;
}

int eh_model_state(struct esphome_snapshot *model, uint32_t type,
                   const void *body, size_t length)
{
  const struct entity_schema *schema = NULL;
  struct eh_pb_reader r = {body, length, 0};
  struct eh_pb_field f;
  struct esphome_entity value = {0};
  bool key_seen = false;
  bool missing = false;
  int ret;
  for (size_t i = 0; i < sizeof(schemas) / sizeof(schemas[0]); ++i)
    if (type == schemas[i].state) schema = &schemas[i];
  if (!schema) return 0;
  while ((ret = eh_pb_next(&r, &f)) > 0)
    {
      if (f.number == EH_SensorStateResponse_key)
        {
          if (eh_pb_fixed32(&f, &value.key) < 0) return -EPROTO;
          key_seen = true;
        }
      else if (f.number == schema->state_device)
        {
          if (eh_pb_u32(&f, &value.device_id) < 0) return -EPROTO;
        }
      else if (f.number == EH_SensorStateResponse_state)
        {
          if (schema->kind == ESPHOME_SENSOR)
            {
              if (eh_pb_float(&f, &value.value) < 0) return -EPROTO;
            }
          else if (schema->kind == ESPHOME_TEXT_SENSOR)
            {
              if (eh_pb_string(&f, value.text, sizeof(value.text)) < 0)
                return -EPROTO;
            }
          else
            {
              uint32_t state;
              if (eh_pb_u32(&f, &state) < 0) return -EPROTO;
              value.state = state != 0;
            }
        }
      else if (schema->kind == ESPHOME_LIGHT &&
               f.number == EH_LightStateResponse_brightness)
        {
          if (eh_pb_float(&f, &value.brightness) < 0) return -EPROTO;
        }
      else if ((schema->kind == ESPHOME_SENSOR ||
                schema->kind == ESPHOME_BINARY_SENSOR ||
                schema->kind == ESPHOME_TEXT_SENSOR) &&
               f.number == EH_SensorStateResponse_missing_state)
        {
          uint32_t state;
          if (eh_pb_u32(&f, &state) < 0) return -EPROTO;
          missing = state != 0;
        }
    }
  if (ret < 0 || !key_seen) return -EPROTO;
  for (size_t i = 0; i < model->count; ++i)
    {
      struct esphome_entity *entity = &model->entities[i];
      if (entity->key != value.key || entity->device_id != value.device_id ||
          entity->kind != schema->kind) continue;
      entity->has_state = !missing;
      entity->state = value.state;
      entity->value = value.value;
      entity->brightness = value.brightness;
      memcpy(entity->text, value.text, sizeof(entity->text));
      if (schema->kind == ESPHOME_SENSOR && !isfinite(value.value))
        entity->has_state = false;
      if (schema->kind == ESPHOME_LIGHT &&
          (!isfinite(value.brightness) || value.brightness < 0 ||
           value.brightness > 1)) entity->has_state = false;
      ++model->revision;
      break;
    }
  return 1;
}

int eh_model_command(const struct esphome_entity *entity, bool state,
                     bool has_brightness, float brightness,
                     struct eh_pb_writer *w, uint32_t *type)
{
  if (!entity->has_state) return -EAGAIN;
  if (has_brightness && (!isfinite(brightness) || brightness < 0 ||
                         brightness > 1)) return -EINVAL;
  if (has_brightness && (entity->kind != ESPHOME_LIGHT ||
                         !entity->supports_brightness)) return -ENOTSUP;
  if (entity->kind == ESPHOME_SWITCH)
    {
      *type = EH_SwitchCommandRequest;
      eh_pb_put_fixed32(w, EH_SwitchCommandRequest_key, entity->key);
      eh_pb_put_uint(w, EH_SwitchCommandRequest_state, state);
      eh_pb_put_uint(w, EH_SwitchCommandRequest_device_id, entity->device_id);
    }
  else if (entity->kind == ESPHOME_LIGHT)
    {
      *type = EH_LightCommandRequest;
      eh_pb_put_fixed32(w, EH_LightCommandRequest_key, entity->key);
      eh_pb_put_uint(w, EH_LightCommandRequest_has_state, 1);
      eh_pb_put_uint(w, EH_LightCommandRequest_state, state);
      if (has_brightness)
        {
          eh_pb_put_uint(w, EH_LightCommandRequest_has_brightness, 1);
          eh_pb_put_float(w, EH_LightCommandRequest_brightness, brightness);
        }
      eh_pb_put_uint(w, EH_LightCommandRequest_device_id, entity->device_id);
    }
  else return -ENOTSUP;
  return w->error;
}
