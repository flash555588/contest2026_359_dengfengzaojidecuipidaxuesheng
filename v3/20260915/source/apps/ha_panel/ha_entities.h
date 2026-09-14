/****************************************************************************
 * apps/ha_panel/ha_entities.h
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

#ifndef __APPS_HA_PANEL_HA_ENTITIES_H
#  define __APPS_HA_PANEL_HA_ENTITIES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>
#include <semaphore.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct cJSON;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_HA_PANEL_MAX_ENTITIES
#  define CONFIG_HA_PANEL_MAX_ENTITIES 64
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum ha_domain_e
{
  HA_DOMAIN_UNKNOWN = 0,
  HA_DOMAIN_LIGHT,
  HA_DOMAIN_SWITCH,
  HA_DOMAIN_INPUT_BOOLEAN,
  HA_DOMAIN_SENSOR,
  HA_DOMAIN_BINARY_SENSOR,
  HA_DOMAIN_CLIMATE,
  HA_DOMAIN_COVER,
  HA_DOMAIN_SCENE,
  HA_DOMAIN_SCRIPT,
  HA_DOMAIN_BUTTON,
  HA_DOMAIN_FAN,
  HA_DOMAIN_LOCK,
  HA_DOMAIN_MEDIA_PLAYER,
  HA_DOMAIN_VACUUM,
  HA_DOMAIN_AUTOMATION
};

enum ha_area_e
{
  HA_AREA_OTHER = 0,
  HA_AREA_LIVING,
  HA_AREA_KITCHEN,
  HA_AREA_BEDROOM,
  HA_AREA_OUTDOOR
};

struct ha_entity_s
{
  char entity_id[64];
  char friendly_name[64];
  char state[32];
  char unit[16];
  char hvac_mode[16];
  enum ha_domain_e domain;
  enum ha_area_e area;
  bool available;
  int brightness;   /* 0-255, -1 if n/a */
  int position;     /* 0-100, -1 if n/a */
  int temp_x10;     /* current temperature * 10 */
  int target_x10;   /* target temperature * 10 */
};

struct ha_store_s
{
  sem_t lock;
  int count;
  char ha_version[32];
  struct ha_entity_s items[CONFIG_HA_PANEL_MAX_ENTITIES];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

enum ha_domain_e ha_domain_from_id(FAR const char *entity_id);
FAR const char *ha_domain_name(enum ha_domain_e domain);
enum ha_area_e ha_area_from_id(FAR const char *entity_id);
FAR const char *ha_area_name(enum ha_area_e area);
bool ha_entity_is_on(FAR const struct ha_entity_s *ent);
int ha_store_apply_service(FAR struct ha_store_s *store,
                           FAR const char *domain, FAR const char *service,
                           FAR const char *entity_id, FAR const char *extra);
int ha_store_apply_scene(FAR struct ha_store_s *store,
                         FAR const char *scene_id);
int ha_store_lights_all(FAR struct ha_store_s *store, bool on);

int ha_store_init(FAR struct ha_store_s *store);
void ha_store_lock(FAR struct ha_store_s *store);
void ha_store_unlock(FAR struct ha_store_s *store);
void ha_store_clear(FAR struct ha_store_s *store);
int ha_store_upsert(FAR struct ha_store_s *store,
                    FAR const struct ha_entity_s *ent);
int ha_store_ingest_state(FAR struct ha_store_s *store, FAR struct cJSON *obj);
int ha_store_snapshot(FAR struct ha_store_s *store,
                      FAR struct ha_entity_s *out, int maxcount);
FAR struct ha_entity_s *ha_store_find(FAR struct ha_store_s *store,
                                      FAR const char *entity_id);
void ha_store_load_demo(FAR struct ha_store_s *store);
int ha_store_count_on(FAR struct ha_store_s *store, enum ha_domain_e domain);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_HA_PANEL_HA_ENTITIES_H */
