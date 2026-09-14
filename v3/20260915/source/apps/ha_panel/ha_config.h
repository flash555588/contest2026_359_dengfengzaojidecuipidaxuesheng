/****************************************************************************
 * apps/ha_panel/ha_config.h
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

#ifndef __APPS_HA_PANEL_HA_CONFIG_H
#  define __APPS_HA_PANEL_HA_CONFIG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct ha_config_s
{
  char server[128];
  uint16_t port;
  char token[512];
  char path[160];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void ha_config_init(FAR struct ha_config_s *cfg);
int ha_config_load(FAR struct ha_config_s *cfg);
int ha_config_save(FAR const struct ha_config_s *cfg);
int ha_config_apply_args(FAR struct ha_config_s *cfg, int argc,
                         FAR char *argv[]);
void ha_config_usage(FAR const char *prog);

#endif /* __APPS_HA_PANEL_HA_CONFIG_H */
