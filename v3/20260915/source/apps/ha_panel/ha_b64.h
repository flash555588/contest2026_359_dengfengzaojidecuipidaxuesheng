/****************************************************************************
 * apps/ha_panel/ha_b64.h
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

#ifndef __APPS_HA_PANEL_HA_B64_H
#  define __APPS_HA_PANEL_HA_B64_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stddef.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Number of bytes required to base64-encode "len" bytes, including the
 * terminating NUL.
 */

size_t ha_b64_encoded_len(size_t len);

/* Base64 encode src.  Returns the number of characters written (not
 * counting the NUL), or a negative errno value if dst is too small.
 */

ssize_t ha_b64_encode(FAR const uint8_t *src, size_t len, FAR char *dst,
                      size_t dstlen);

#endif /* __APPS_HA_PANEL_HA_B64_H */
