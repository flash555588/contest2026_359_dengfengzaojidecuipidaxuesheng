/****************************************************************************
 * apps/ha_panel/ha_ws_frame.h
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

#ifndef __APPS_HA_PANEL_HA_WS_FRAME_H
#  define __APPS_HA_PANEL_HA_WS_FRAME_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* WebSocket frame opcodes (RFC 6455 section 5.2) */

#define HA_WS_OP_CONT   0x0
#define HA_WS_OP_TEXT   0x1
#define HA_WS_OP_BINARY 0x2
#define HA_WS_OP_CLOSE  0x8
#define HA_WS_OP_PING   0x9
#define HA_WS_OP_PONG   0xa

/* Maximum size of a WebSocket frame header */

#define HA_WS_MAX_HEADER 14

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Incremental frame header decoder.  Feed incoming bytes with
 * ha_ws_header_feed(); once it returns HA_WS_DECODE_HEADER the frame header
 * fields below are valid and the remaining payload_len bytes can be read
 * from the stream directly.
 */

enum ha_ws_decode_e
{
  HA_WS_DECODE_MORE = 0,   /* Call again with more bytes */
  HA_WS_DECODE_HEADER,     /* Header complete, payload follows */
  HA_WS_DECODE_ERROR       /* Protocol error, connection unusable */
};

struct ha_ws_decoder_s
{
  int state;                          /* Internal parse state */
  uint8_t hdr[HA_WS_MAX_HEADER];      /* Partially received header */
  size_t hdrlen;                      /* Bytes of header received */
  size_t hdrneed;                     /* Total header bytes expected */
  bool masked;
  uint8_t maskkey[4];
  uint64_t payload_len;
  uint8_t opcode;
  bool fin;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void ha_ws_decoder_init(FAR struct ha_ws_decoder_s *dec);

/* Feed "len" bytes from "data" into the decoder.  Returns the decoder
 * state.  When HA_WS_DECODE_HEADER is returned, *consumed receives the
 * number of input bytes used by the header; the remaining bytes belong
 * to the frame payload.
 */

int ha_ws_header_feed(FAR struct ha_ws_decoder_s *dec,
                      FAR const uint8_t *data, size_t len,
                      FAR size_t *consumed);

/* Unmask "len" payload bytes in place.  "offset" is the index of data[0]
 * inside the whole frame payload; it keeps the 4-byte mask rotating
 * across multiple receive chunks.
 */

void ha_ws_unmask(FAR uint8_t *data, size_t len,
                  FAR const uint8_t maskkey[4], uint64_t offset);

/* Build a masked client frame (FIN set) into out.  Returns 0 on success,
 * a negative errno value if out is too small.  The full frame size is
 * stored in *outsize.
 */

int ha_ws_frame_encode(uint8_t opcode, FAR const uint8_t *payload,
                       size_t len, FAR const uint8_t maskkey[4],
                       FAR uint8_t *out, size_t outlen,
                       FAR size_t *outsize);

#endif /* __APPS_HA_PANEL_HA_WS_FRAME_H */
