/****************************************************************************
 * apps/ha_panel/ha_ws.h
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

#ifndef __APPS_HA_PANEL_HA_WS_H
#  define __APPS_HA_PANEL_HA_WS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ha_ws_frame.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct ha_ws_s
{
  int sockfd;

  /* Frame decode state (kept across partial reads so that -ETIMEDOUT is
   * recoverable by simply calling ha_ws_recv_message() again).
   */

  struct ha_ws_decoder_s dec;
  bool header_ready;
  uint64_t payload_left;
  uint64_t payload_off;
  bool discarding;
  bool msg_active;

  /* Message assembly buffer (largest permitted message + NUL) */

  uint8_t *msg;
  size_t msglen;
  size_t msgcap;

  uint8_t pingbuf[125];
  size_t pinglen;

  uint8_t scratch[512];

  /* Bytes left over from a recv() that contained more than one frame */

  uint8_t hold[512];
  size_t holdlen;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Connect to ws://host:port/path and perform the RFC 6455 handshake.
 * Returns 0 on success or a negative errno value.
 */

int ha_ws_connect(FAR struct ha_ws_s *ws, FAR const char *host,
                  uint16_t port, FAR const char *path, size_t rxmax,
                  long timeout_ms);

/* Close the connection and release buffers.  Safe to call on an
 * unconnected session.
 */

void ha_ws_close(FAR struct ha_ws_s *ws);

/* Send one text message.  Returns 0 or a negative errno value. */

int ha_ws_send_text(FAR struct ha_ws_s *ws, FAR const char *payload);

/* Receive the next complete text message.  Returns 0 and sets *out to the
 * zero terminated message; the buffer is owned by the session and stays
 * valid until the next ha_ws_recv_message()/ha_ws_send_text()/close call.
 *
 * Special return values:
 *   -ETIMEDOUT  no complete message within timeout_ms; decode state is
 *               preserved, simply call again.
 *   -EMSGSIZE   an incoming text message exceeded the receive limit and
 *               was discarded; the connection remains usable.
 *   -ECONNRESET the peer closed the connection.
 *   -EPROTO     protocol violation.
 */

int ha_ws_recv_message(FAR struct ha_ws_s *ws, FAR char **out,
                       long timeout_ms);

#endif /* __APPS_HA_PANEL_HA_WS_H */
