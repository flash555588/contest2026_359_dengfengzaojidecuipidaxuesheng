/****************************************************************************
 * apps/ha_panel/ha_client.h
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

#ifndef __APPS_HA_PANEL_HA_CLIENT_H
#  define __APPS_HA_PANEL_HA_CLIENT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include "ha_config.h"
#include "ha_entities.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum ha_conn_e
{
  HA_CONN_IDLE = 0,
  HA_CONN_DEMO,
  HA_CONN_CONNECTING,
  HA_CONN_AUTH,
  HA_CONN_LIVE,
  HA_CONN_ERROR
};

enum ha_evt_type_e
{
  HA_EVT_CONN = 0,
  HA_EVT_STORE,
  HA_EVT_ERROR
};

struct ha_evt_s
{
  enum ha_evt_type_e type;
  enum ha_conn_e conn;
  char text[96];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int ha_client_start(FAR struct ha_config_s *cfg,
                    FAR struct ha_store_s *store);
void ha_client_stop(void);
void ha_client_reconnect(void);
int ha_client_call(FAR const char *domain, FAR const char *service,
                   FAR const char *entity_id, FAR const char *extra);
int ha_client_poll_evt(FAR struct ha_evt_s *evt);
enum ha_conn_e ha_client_conn(void);

#endif /* __APPS_HA_PANEL_HA_CLIENT_H */
