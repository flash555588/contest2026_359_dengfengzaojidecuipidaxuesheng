/****************************************************************************
 * apps/ha_panel/ha_entities.c
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netutils/cJSON.h"
#include "ha_entities.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void cpy(FAR char *dst, size_t dstlen, FAR const char *src)
{
  if (src == NULL)
    {
      dst[0] = '\0';
      return;
    }

  strncpy(dst, src, dstlen - 1);
  dst[dstlen - 1] = '\0';
}

static FAR const char *json_str(FAR cJSON *obj, FAR const char *key)
{
  FAR cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);

  if (cJSON_IsString(v) && v->valuestring != NULL)
    {
      return v->valuestring;
    }

  return NULL;
}

static int json_int(FAR cJSON *obj, FAR const char *key, int missing)
{
  FAR cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);

  if (cJSON_IsNumber(v))
    {
      return (int)v->valuedouble;
    }

  return missing;
}

static int json_x10(FAR cJSON *obj, FAR const char *key)
{
  FAR cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);

  if (cJSON_IsNumber(v))
    {
      return (int)(v->valuedouble * 10.0);
    }

  return -32768;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

enum ha_domain_e ha_domain_from_id(FAR const char *entity_id)
{
  if (entity_id == NULL)
    {
      return HA_DOMAIN_UNKNOWN;
    }

  if (strncmp(entity_id, "light.", 6) == 0)
    {
      return HA_DOMAIN_LIGHT;
    }

  if (strncmp(entity_id, "switch.", 7) == 0)
    {
      return HA_DOMAIN_SWITCH;
    }

  if (strncmp(entity_id, "input_boolean.", 14) == 0)
    {
      return HA_DOMAIN_INPUT_BOOLEAN;
    }

  if (strncmp(entity_id, "binary_sensor.", 14) == 0)
    {
      return HA_DOMAIN_BINARY_SENSOR;
    }

  if (strncmp(entity_id, "sensor.", 7) == 0)
    {
      return HA_DOMAIN_SENSOR;
    }

  if (strncmp(entity_id, "climate.", 8) == 0)
    {
      return HA_DOMAIN_CLIMATE;
    }

  if (strncmp(entity_id, "cover.", 6) == 0)
    {
      return HA_DOMAIN_COVER;
    }

  if (strncmp(entity_id, "scene.", 6) == 0)
    {
      return HA_DOMAIN_SCENE;
    }

  if (strncmp(entity_id, "script.", 7) == 0)
    {
      return HA_DOMAIN_SCRIPT;
    }

  if (strncmp(entity_id, "button.", 7) == 0 ||
      strncmp(entity_id, "input_button.", 13) == 0)
    {
      return HA_DOMAIN_BUTTON;
    }

  if (strncmp(entity_id, "fan.", 4) == 0)
    {
      return HA_DOMAIN_FAN;
    }

  if (strncmp(entity_id, "lock.", 5) == 0)
    {
      return HA_DOMAIN_LOCK;
    }

  if (strncmp(entity_id, "media_player.", 13) == 0)
    {
      return HA_DOMAIN_MEDIA_PLAYER;
    }

  if (strncmp(entity_id, "vacuum.", 7) == 0)
    {
      return HA_DOMAIN_VACUUM;
    }

  if (strncmp(entity_id, "automation.", 11) == 0)
    {
      return HA_DOMAIN_AUTOMATION;
    }

  return HA_DOMAIN_UNKNOWN;
}

enum ha_area_e ha_area_from_id(FAR const char *entity_id)
{
  FAR const char *obj;

  if (entity_id == NULL)
    {
      return HA_AREA_OTHER;
    }

  obj = strchr(entity_id, '.');
  obj = (obj != NULL) ? obj + 1 : entity_id;

  if (strstr(obj, "living") != NULL || strstr(obj, "lounge") != NULL ||
      strstr(obj, "tv") != NULL)
    {
      return HA_AREA_LIVING;
    }

  if (strstr(obj, "kitchen") != NULL)
    {
      return HA_AREA_KITCHEN;
    }

  if (strstr(obj, "bed") != NULL)
    {
      return HA_AREA_BEDROOM;
    }

  if (strstr(obj, "garage") != NULL || strstr(obj, "porch") != NULL ||
      strstr(obj, "yard") != NULL || strstr(obj, "outdoor") != NULL ||
      strstr(obj, "leave") != NULL)
    {
      return HA_AREA_OUTDOOR;
    }

  return HA_AREA_OTHER;
}

FAR const char *ha_area_name(enum ha_area_e area)
{
  switch (area)
    {
      case HA_AREA_LIVING:  return "living";
      case HA_AREA_KITCHEN: return "kitchen";
      case HA_AREA_BEDROOM: return "bedroom";
      case HA_AREA_OUTDOOR: return "outdoor";
      default:              return "other";
    }
}

FAR const char *ha_domain_name(enum ha_domain_e domain)
{
  switch (domain)
    {
      case HA_DOMAIN_LIGHT:          return "light";
      case HA_DOMAIN_SWITCH:         return "switch";
      case HA_DOMAIN_INPUT_BOOLEAN:  return "input_boolean";
      case HA_DOMAIN_SENSOR:         return "sensor";
      case HA_DOMAIN_BINARY_SENSOR:  return "binary_sensor";
      case HA_DOMAIN_CLIMATE:        return "climate";
      case HA_DOMAIN_COVER:          return "cover";
      case HA_DOMAIN_SCENE:          return "scene";
      case HA_DOMAIN_SCRIPT:         return "script";
      case HA_DOMAIN_BUTTON:         return "button";
      case HA_DOMAIN_FAN:            return "fan";
      case HA_DOMAIN_LOCK:           return "lock";
      case HA_DOMAIN_MEDIA_PLAYER:   return "media_player";
      case HA_DOMAIN_VACUUM:         return "vacuum";
      case HA_DOMAIN_AUTOMATION:     return "automation";
      default:                       return "unknown";
    }
}

bool ha_entity_is_on(FAR const struct ha_entity_s *ent)
{
  if (ent == NULL)
    {
      return false;
    }

  if (strcmp(ent->state, "on") == 0 ||
      strcmp(ent->state, "open") == 0 ||
      strcmp(ent->state, "opening") == 0 ||
      strcmp(ent->state, "playing") == 0 ||
      strcmp(ent->state, "unlocked") == 0 ||
      strcmp(ent->state, "heat") == 0 ||
      strcmp(ent->state, "cool") == 0 ||
      strcmp(ent->state, "auto") == 0 ||
      strcmp(ent->state, "heat_cool") == 0 ||
      strcmp(ent->state, "locked") == 0 ||
      strcmp(ent->state, "playing") == 0 ||
      strcmp(ent->state, "cleaning") == 0)
    {
      return true;
    }

  return false;
}

int ha_store_init(FAR struct ha_store_s *store)
{
  memset(store, 0, sizeof(*store));
  return sem_init(&store->lock, 0, 1);
}

void ha_store_lock(FAR struct ha_store_s *store)
{
  while (sem_wait(&store->lock) < 0 && errno == EINTR)
    {
    }
}

void ha_store_unlock(FAR struct ha_store_s *store)
{
  sem_post(&store->lock);
}

void ha_store_clear(FAR struct ha_store_s *store)
{
  ha_store_lock(store);
  store->count = 0;
  memset(store->items, 0, sizeof(store->items));
  ha_store_unlock(store);
}

FAR struct ha_entity_s *ha_store_find(FAR struct ha_store_s *store,
                                      FAR const char *entity_id)
{
  int i;

  for (i = 0; i < store->count; i++)
    {
      if (strcmp(store->items[i].entity_id, entity_id) == 0)
        {
          return &store->items[i];
        }
    }

  return NULL;
}

int ha_store_upsert(FAR struct ha_store_s *store,
                    FAR const struct ha_entity_s *ent)
{
  FAR struct ha_entity_s *dst;

  if (ent == NULL || ent->entity_id[0] == '\0' ||
      ent->domain == HA_DOMAIN_UNKNOWN)
    {
      return 0;
    }

  ha_store_lock(store);
  dst = ha_store_find(store, ent->entity_id);
  if (dst == NULL)
    {
      if (store->count >= CONFIG_HA_PANEL_MAX_ENTITIES)
        {
          ha_store_unlock(store);
          return -ENOSPC;
        }

      dst = &store->items[store->count++];
    }

  *dst = *ent;
  ha_store_unlock(store);
  return 1;
}

int ha_store_ingest_state(FAR struct ha_store_s *store, FAR struct cJSON *obj)
{
  struct ha_entity_s ent;
  FAR cJSON *attrs;
  FAR cJSON *cat;
  FAR const char *id;
  FAR const char *name;
  FAR const char *unit;
  FAR const char *hvac;

  if (obj == NULL)
    {
      return 0;
    }

  memset(&ent, 0, sizeof(ent));
  ent.brightness = -1;
  ent.position = -1;
  ent.temp_x10 = -32768;
  ent.target_x10 = -32768;

  id = json_str(obj, "entity_id");
  if (id == NULL)
    {
      return 0;
    }

  ent.domain = ha_domain_from_id(id);
  if (ent.domain == HA_DOMAIN_UNKNOWN)
    {
      return 0;
    }

  attrs = cJSON_GetObjectItemCaseSensitive(obj, "attributes");
  cat = attrs ? cJSON_GetObjectItemCaseSensitive(attrs,
                                                 "entity_category") : NULL;
  if (cJSON_IsString(cat) && cat->valuestring != NULL &&
      strcmp(cat->valuestring, "diagnostic") == 0)
    {
      return 0;
    }

  if (attrs != NULL)
    {
      FAR cJSON *hidden = cJSON_GetObjectItemCaseSensitive(attrs, "hidden");

      if (cJSON_IsTrue(hidden))
        {
          return 0;
        }
    }

  cpy(ent.entity_id, sizeof(ent.entity_id), id);
  ent.area = ha_area_from_id(ent.entity_id);
  cpy(ent.state, sizeof(ent.state), json_str(obj, "state"));
  ent.available = (strcmp(ent.state, "unavailable") != 0 &&
                   strcmp(ent.state, "unknown") != 0);

  name = attrs ? json_str(attrs, "friendly_name") : NULL;
  cpy(ent.friendly_name, sizeof(ent.friendly_name),
      name != NULL ? name : id);

  unit = attrs ? json_str(attrs, "unit_of_measurement") : NULL;
  cpy(ent.unit, sizeof(ent.unit), unit);

  if (attrs != NULL)
    {
      ent.brightness = json_int(attrs, "brightness", -1);
      ent.position = json_int(attrs, "current_position", -1);
      if (ent.position < 0)
        {
          ent.position = json_int(attrs, "position", -1);
        }

      ent.temp_x10 = json_x10(attrs, "current_temperature");
      ent.target_x10 = json_x10(attrs, "temperature");
      hvac = json_str(attrs, "hvac_mode");
      cpy(ent.hvac_mode, sizeof(ent.hvac_mode), hvac);
    }

  return ha_store_upsert(store, &ent);
}

int ha_store_snapshot(FAR struct ha_store_s *store,
                      FAR struct ha_entity_s *out, int maxcount)
{
  int n;

  ha_store_lock(store);
  n = store->count;
  if (n > maxcount)
    {
      n = maxcount;
    }

  memcpy(out, store->items, n * sizeof(struct ha_entity_s));
  ha_store_unlock(store);
  return n;
}

void ha_store_load_demo(FAR struct ha_store_s *store)
{
  struct ha_entity_s e;
  int i;
  static const struct
    {
      const char *id;
      const char *name;
      const char *state;
      const char *unit;
      int brightness;
      int position;
      int temp;
      int target;
      const char *hvac;
    } demo[] =
    {
      {
        "light.living_room", "Living room", "on", "", 180, -1, -32768,
        -32768, ""
      },
      {
        "light.kitchen", "Kitchen", "off", "", 0, -1, -32768, -32768, ""
      },
      {
        "light.bedroom", "Bedroom", "off", "", 40, -1, -32768, -32768, ""
      },
      {
        "switch.desk_fan", "Desk fan", "on", "", -1, -1, -32768, -32768, ""
      },
      {
        "fan.bedroom", "Bedroom fan", "off", "", -1, 40, -32768, -32768, ""
      },
      {
        "sensor.temperature", "Indoor temp", "23.5", "°C", -1, -1, 235,
        -32768, ""
      },
      {
        "sensor.humidity", "Humidity", "46", "%", -1, -1, -32768, -32768, ""
      },
      {
        "binary_sensor.front_door", "Front door", "off", "", -1, -1, -32768,
        -32768, ""
      },
      {
        "lock.front_door", "Door lock", "unlocked", "", -1, -1, -32768,
        -32768, ""
      },
      {
        "climate.home", "Climate", "heat", "", -1, -1, 225, 240, "heat"
      },
      {
        "cover.garage", "Garage", "open", "", -1, 70, -32768, -32768, ""
      },
      {
        "cover.living_curtain", "Curtain", "open", "", -1, 100, -32768,
        -32768, ""
      },
      {
        "media_player.living_tv", "Living TV", "idle", "", -1, 40, -32768,
        -32768, ""
      },
      {
        "vacuum.cleaner", "Vacuum", "docked", "", -1, -1, -32768, -32768, ""
      },
      {
        "automation.motion_light", "Motion light", "on", "", -1, -1, -32768,
        -32768, ""
      },
      {
        "scene.movie", "Movie", "off", "", -1, -1, -32768, -32768, ""
      },
      {
        "scene.goodnight", "Good night", "off", "", -1, -1, -32768, -32768, ""
      },
      {
        "scene.leave_home", "Leave home", "off", "", -1, -1, -32768, -32768, ""
      },
      {
        "scene.arrive_home", "Arrive home", "off", "", -1, -1, -32768,
        -32768, ""
      },
      {
        "scene.reading", "Reading", "off", "", -1, -1, -32768, -32768, ""
      }
    };

  ha_store_clear(store);
  cpy(store->ha_version, sizeof(store->ha_version), "demo");

  for (i = 0; i < (int)(sizeof(demo) / sizeof(demo[0])); i++)
    {
      memset(&e, 0, sizeof(e));
      cpy(e.entity_id, sizeof(e.entity_id), demo[i].id);
      cpy(e.friendly_name, sizeof(e.friendly_name), demo[i].name);
      cpy(e.state, sizeof(e.state), demo[i].state);
      cpy(e.unit, sizeof(e.unit), demo[i].unit);
      cpy(e.hvac_mode, sizeof(e.hvac_mode), demo[i].hvac);
      e.domain = ha_domain_from_id(demo[i].id);
      e.area = ha_area_from_id(demo[i].id);
      e.available = true;
      e.brightness = demo[i].brightness;
      e.position = demo[i].position;
      e.temp_x10 = demo[i].temp;
      e.target_x10 = demo[i].target;
      ha_store_upsert(store, &e);
    }
}

int ha_store_count_on(FAR struct ha_store_s *store, enum ha_domain_e domain)
{
  int i;
  int n = 0;

  ha_store_lock(store);
  for (i = 0; i < store->count; i++)
    {
      if (store->items[i].domain == domain &&
          ha_entity_is_on(&store->items[i]))
        {
          n++;
        }
    }

  ha_store_unlock(store);
  return n;
}

static int extra_int(FAR const char *extra, FAR const char *key, int missing)
{
  FAR const char *p;

  if (extra == NULL || extra[0] == '\0')
    {
      return missing;
    }

  p = strstr(extra, key);
  if (p == NULL)
    {
      return missing;
    }

  p = strchr(p, ':');
  if (p == NULL)
    {
      return missing;
    }

  return (int)strtol(p + 1, NULL, 10);
}

static int apply_one(FAR struct ha_entity_s *e, FAR const char *service,
                     FAR const char *extra)
{
  if (e == NULL || service == NULL)
    {
      return 0;
    }

  if (strcmp(service, "turn_on") == 0 || strcmp(service, "lock") == 0 ||
      strcmp(service, "open_cover") == 0)
    {
      if (e->domain == HA_DOMAIN_LOCK)
        {
          cpy(e->state, sizeof(e->state), "locked");
        }
      else if (e->domain == HA_DOMAIN_COVER)
        {
          cpy(e->state, sizeof(e->state), "open");
          e->position = 100;
        }
      else if (e->domain == HA_DOMAIN_CLIMATE)
        {
          cpy(e->state, sizeof(e->state),
              e->hvac_mode[0] != '\0' ? e->hvac_mode : "heat");
        }
      else if (e->domain == HA_DOMAIN_LIGHT)
        {
          int bri = extra_int(extra, "brightness", e->brightness);

          cpy(e->state, sizeof(e->state), "on");
          if (bri >= 0)
            {
              e->brightness = bri;
            }
          else if (e->brightness <= 0)
            {
              e->brightness = 180;
            }
        }
      else if (e->domain == HA_DOMAIN_MEDIA_PLAYER)
        {
          cpy(e->state, sizeof(e->state), "playing");
        }
      else if (e->domain == HA_DOMAIN_VACUUM)
        {
          cpy(e->state, sizeof(e->state), "cleaning");
        }
      else
        {
          cpy(e->state, sizeof(e->state), "on");
        }
    }
  else if (strcmp(service, "turn_off") == 0 ||
           strcmp(service, "unlock") == 0 ||
           strcmp(service, "close_cover") == 0)
    {
      if (e->domain == HA_DOMAIN_LOCK)
        {
          cpy(e->state, sizeof(e->state), "unlocked");
        }
      else if (e->domain == HA_DOMAIN_COVER)
        {
          cpy(e->state, sizeof(e->state), "closed");
          e->position = 0;
        }
      else if (e->domain == HA_DOMAIN_CLIMATE)
        {
          cpy(e->state, sizeof(e->state), "off");
          cpy(e->hvac_mode, sizeof(e->hvac_mode), "off");
        }
      else
        {
          if (e->domain == HA_DOMAIN_MEDIA_PLAYER)
            {
              cpy(e->state, sizeof(e->state), "idle");
            }
          else if (e->domain == HA_DOMAIN_VACUUM)
            {
              cpy(e->state, sizeof(e->state), "docked");
            }
          else
            {
              cpy(e->state, sizeof(e->state), "off");
            }

          if (e->domain == HA_DOMAIN_LIGHT)
            {
              e->brightness = 0;
            }
        }
    }
  else if (strcmp(service, "toggle") == 0)
    {
      return apply_one(e, ha_entity_is_on(e) ? "turn_off" : "turn_on", extra);
    }
  else if (strcmp(service, "stop_cover") == 0)
    {
      cpy(e->state, sizeof(e->state), "stopped");
    }
  else if (strcmp(service, "set_temperature") == 0)
    {
      int t = extra_int(extra, "temperature", -1);

      if (t > 0)
        {
          e->target_x10 = t * 10;
        }

      if (strcmp(e->state, "off") == 0)
        {
          cpy(e->state, sizeof(e->state), "heat");
          cpy(e->hvac_mode, sizeof(e->hvac_mode), "heat");
        }
    }
  else if (strcmp(service, "set_hvac_mode") == 0)
    {
      FAR const char *p = extra ? strstr(extra, "hvac_mode") : NULL;

      if (p != NULL)
        {
          p = strchr(p, ':');
          if (p != NULL)
            {
              p = strchr(p, '"');
            }

          if (p != NULL)
            {
              char mode[16];
              size_t n;

              p++;
              n = strcspn(p, "\"");
              if (n >= sizeof(mode))
                {
                  n = sizeof(mode) - 1;
                }

              memcpy(mode, p, n);
              mode[n] = '\0';
              cpy(e->hvac_mode, sizeof(e->hvac_mode), mode);
              cpy(e->state, sizeof(e->state), mode);
            }
        }
    }
  else if (strcmp(service, "media_play_pause") == 0)
    {
      cpy(e->state, sizeof(e->state),
          strcmp(e->state, "playing") == 0 ? "paused" : "playing");
    }
  else if (strcmp(service, "volume_up") == 0)
    {
      if (e->position < 0)
        {
          e->position = 40;
        }

      e->position += 10;
      if (e->position > 100)
        {
          e->position = 100;
        }
    }
  else if (strcmp(service, "volume_down") == 0)
    {
      if (e->position < 0)
        {
          e->position = 40;
        }

      e->position -= 10;
      if (e->position < 0)
        {
          e->position = 0;
        }
    }
  else if (strcmp(service, "start") == 0)
    {
      cpy(e->state, sizeof(e->state), "cleaning");
    }
  else if (strcmp(service, "return_to_base") == 0)
    {
      cpy(e->state, sizeof(e->state), "docked");
    }
  else if (strcmp(service, "stop") == 0)
    {
      cpy(e->state, sizeof(e->state), "idle");
    }
  else if (strcmp(service, "press") == 0)
    {
      /* Momentary; leave state unchanged. */
    }
  else
    {
      return 0;
    }

  e->available = true;
  return 1;
}

int ha_store_apply_service(FAR struct ha_store_s *store,
                           FAR const char *domain, FAR const char *service,
                           FAR const char *entity_id, FAR const char *extra)
{
  int changed = 0;
  int i;
  enum ha_domain_e want;

  if (store == NULL || service == NULL)
    {
      return 0;
    }

  if (domain != NULL && strcmp(domain, "scene") == 0 &&
      strcmp(service, "turn_on") == 0)
    {
      return ha_store_apply_scene(store, entity_id);
    }

  want = ha_domain_from_id(entity_id);
  if (want == HA_DOMAIN_UNKNOWN && domain != NULL)
    {
      char tmp[80];

      snprintf(tmp, sizeof(tmp), "%s.x", domain);
      want = ha_domain_from_id(tmp);
    }

  ha_store_lock(store);
  for (i = 0; i < store->count; i++)
    {
      FAR struct ha_entity_s *e = &store->items[i];
      bool match;

      if (entity_id != NULL && strcmp(entity_id, "all") == 0)
        {
          match = (e->domain == want);
        }
      else
        {
          match = (entity_id != NULL &&
                   strcmp(e->entity_id, entity_id) == 0);
        }

      if (match)
        {
          changed += apply_one(e, service, extra);
        }
    }

  ha_store_unlock(store);
  return changed;
}

static void scene_mark(FAR struct ha_store_s *store, FAR const char *id)
{
  int i;

  ha_store_lock(store);
  for (i = 0; i < store->count; i++)
    {
      if (store->items[i].domain == HA_DOMAIN_SCENE)
        {
          cpy(store->items[i].state, sizeof(store->items[i].state),
              strcmp(store->items[i].entity_id, id) == 0 ? "on" : "off");
        }
    }

  ha_store_unlock(store);
}

int ha_store_apply_scene(FAR struct ha_store_s *store,
                         FAR const char *scene_id)
{
  if (store == NULL || scene_id == NULL)
    {
      return 0;
    }

  scene_mark(store, scene_id);

  if (strstr(scene_id, "movie") != NULL)
    {
      ha_store_apply_service(store, "light", "turn_on",
                             "light.living_room", "\"brightness\":80");
      ha_store_apply_service(store, "light", "turn_off",
                             "light.kitchen", NULL);
      ha_store_apply_service(store, "light", "turn_off",
                             "light.bedroom", NULL);
      ha_store_apply_service(store, "cover", "close_cover",
                             "cover.living_curtain", NULL);
      ha_store_apply_service(store, "media_player", "turn_on",
                             "media_player.living_tv", NULL);
      ha_store_apply_service(store, "climate", "set_temperature",
                             "climate.home", "\"temperature\":24");
    }
  else if (strstr(scene_id, "goodnight") != NULL ||
           strstr(scene_id, "night") != NULL)
    {
      ha_store_lights_all(store, false);
      ha_store_apply_service(store, "cover", "close_cover",
                             "cover.living_curtain", NULL);
      ha_store_apply_service(store, "lock", "lock",
                             "lock.front_door", NULL);
      ha_store_apply_service(store, "climate", "set_temperature",
                             "climate.home", "\"temperature\":22");
      ha_store_apply_service(store, "fan", "turn_off",
                             "fan.bedroom", NULL);
    }
  else if (strstr(scene_id, "leave") != NULL)
    {
      ha_store_lights_all(store, false);
      ha_store_apply_service(store, "lock", "lock",
                             "lock.front_door", NULL);
      ha_store_apply_service(store, "cover", "close_cover",
                             "cover.garage", NULL);
      ha_store_apply_service(store, "climate", "turn_off",
                             "climate.home", NULL);
      ha_store_apply_service(store, "media_player", "turn_off",
                             "media_player.living_tv", NULL);
    }
  else if (strstr(scene_id, "arrive") != NULL ||
           strstr(scene_id, "home") != NULL)
    {
      ha_store_apply_service(store, "light", "turn_on",
                             "light.living_room", "\"brightness\":180");
      ha_store_apply_service(store, "light", "turn_on",
                             "light.kitchen", "\"brightness\":128");
      ha_store_apply_service(store, "lock", "unlock",
                             "lock.front_door", NULL);
      ha_store_apply_service(store, "climate", "turn_on",
                             "climate.home", NULL);
      ha_store_apply_service(store, "climate", "set_hvac_mode",
                             "climate.home", "\"hvac_mode\":\"heat\"");
      ha_store_apply_service(store, "climate", "set_temperature",
                             "climate.home", "\"temperature\":23");
    }
  else if (strstr(scene_id, "reading") != NULL ||
           strstr(scene_id, "read") != NULL)
    {
      ha_store_apply_service(store, "light", "turn_on",
                             "light.living_room", "\"brightness\":220");
      ha_store_apply_service(store, "light", "turn_off",
                             "light.kitchen", NULL);
      ha_store_apply_service(store, "light", "turn_on",
                             "light.bedroom", "\"brightness\":90");
      ha_store_apply_service(store, "media_player", "turn_off",
                             "media_player.living_tv", NULL);
    }
  else
    {
      return 1;
    }

  return 1;
}

int ha_store_lights_all(FAR struct ha_store_s *store, bool on)
{
  return ha_store_apply_service(store, "light",
                                on ? "turn_on" : "turn_off", "all",
                                on ? "\"brightness\":180" : NULL);
}
