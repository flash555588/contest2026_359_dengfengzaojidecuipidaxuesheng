/****************************************************************************
 * apps/ha_panel/ha_ui.h
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

#ifndef __APPS_HA_PANEL_HA_UI_H
#  define __APPS_HA_PANEL_HA_UI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "ha_client.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int ha_ui_create(FAR struct ha_store_s *store, FAR struct ha_config_s *cfg);
void ha_ui_apply_evt(FAR const struct ha_evt_s *evt);

#endif /* __APPS_HA_PANEL_HA_UI_H */
