#include "esphome_model.h"
#include "protocol_ids.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct esphome_snapshot model;
int main(void)
{
  uint8_t bytes[256];
  struct eh_pb_writer w = {bytes, sizeof(bytes), 0, 0};
  uint32_t type = 0;
  eh_pb_put_fixed32(&w, EH_ListEntitiesLightResponse_key, 123);
  eh_pb_put_string(&w, EH_ListEntitiesLightResponse_name, "Desk");
  eh_pb_put_uint(&w, EH_ListEntitiesLightResponse_device_id, 7);
  eh_pb_put_uint(&w, EH_ListEntitiesLightResponse_supported_color_modes, 3);
  assert(eh_model_info(&model, EH_ListEntitiesLightResponse, bytes, w.size) == 1);
  assert(model.count == 1 && model.entities[0].supports_brightness);
  assert(eh_model_info(&model, EH_ListEntitiesLightResponse, bytes, w.size) == -EEXIST);
  w.size = 0;
  assert(eh_model_command(&model.entities[0], true, false, 0, &w, &type) == -EAGAIN);
  eh_pb_put_fixed32(&w, EH_LightStateResponse_key, 123);
  eh_pb_put_uint(&w, EH_LightStateResponse_device_id, 8);
  assert(eh_model_state(&model, EH_LightStateResponse, bytes, w.size) == 1);
  assert(!model.entities[0].has_state);
  w.size = 0;
  eh_pb_put_fixed32(&w, EH_LightStateResponse_key, 123);
  eh_pb_put_uint(&w, EH_LightStateResponse_device_id, 7);
  eh_pb_put_float(&w, EH_LightStateResponse_brightness, 0.5f);
  assert(eh_model_state(&model, EH_LightStateResponse, bytes, w.size) == 1);
  assert(model.entities[0].has_state && model.entities[0].brightness == 0.5f);
  w.size = 0;
  assert(eh_model_command(&model.entities[0], true, true, NAN, &w, &type) == -EINVAL);
  assert(eh_model_command(&model.entities[0], true, true, 0.25f, &w, &type) == 0);
  assert(type == EH_LightCommandRequest);
  struct eh_pb_reader r = {bytes, w.size, 0};
  struct eh_pb_field f;
  bool device_seen = false;
  while (eh_pb_next(&r, &f) > 0)
    if (f.number == EH_LightCommandRequest_device_id)
      {assert(f.value == 7); device_seen = true;}
  assert(device_seen);
  const uint8_t bad[] = {0x0a, 0xff};
  r = (struct eh_pb_reader){bad, sizeof(bad), 0};
  assert(eh_pb_next(&r, &f) == -EPROTO);
  puts("model tests passed");
  return 0;
}
