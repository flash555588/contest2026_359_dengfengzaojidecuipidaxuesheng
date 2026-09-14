/****************************************************************************
 * apps/ha_panel/ha_ws.c
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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "ha_b64.h"
#include "ha_sha1.h"
#include "ha_ws.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_HANDSHAKE_TIMEOUT_MS 5000
#define WS_RECV_SLICE_MS        1000
#define WS_MAX_HTTP_RESPONSE    4096

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t mono_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int ws_send_all(FAR struct ha_ws_s *ws, FAR const uint8_t *buf,
                       size_t len)
{
  size_t off = 0;

  while (off < len)
    {
      ssize_t r = send(ws->sockfd, buf + off, len - off, 0);

      if (r < 0)
        {
          int err = errno;

          if (err == EINTR)
            {
              continue;
            }

          return -err;
        }

      off += r;
    }

  return 0;
}

static int ws_send_frame(FAR struct ha_ws_s *ws, uint8_t opcode,
                         FAR const uint8_t *payload, size_t len)
{
  uint8_t key[4];
  uint8_t frame[HA_WS_MAX_HEADER + 125];
  size_t framelen;
  int ret;

  key[0] = (uint8_t)rand();
  key[1] = (uint8_t)rand();
  key[2] = (uint8_t)rand();
  key[3] = (uint8_t)rand();

  /* Control frame payloads fit the small stack frame buffer; data frames
   * of ha_panel are small too (service calls only).
   */

  if (len > sizeof(frame) - HA_WS_MAX_HEADER)
    {
      return -EINVAL;
    }

  ret = ha_ws_frame_encode(opcode, payload, len, key, frame,
                           sizeof(frame), &framelen);
  if (ret < 0)
    {
      return ret;
    }

  return ws_send_all(ws, frame, framelen);
}

static int ws_tcp_connect(FAR const char *host, uint16_t port,
                          long timeout_ms)
{
  struct sockaddr_in addr;
  struct pollfd pfd;
  socklen_t errlen;
  int err = 0;
  int fd;
  int flags;
  int ret;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
#ifdef CONFIG_LIBC_NETDB
      struct addrinfo hints;
      struct addrinfo *res;
      char portstr[8];

      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

      ret = getaddrinfo(host, portstr, &hints, &res);
      if (ret != 0)
        {
          return -EADDRNOTAVAIL;
        }

      memcpy(&addr.sin_addr,
             &((FAR struct sockaddr_in *)res->ai_addr)->sin_addr,
             sizeof(addr.sin_addr));
      freeaddrinfo(res);
#else
      return -EADDRNOTAVAIL;
#endif
    }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      return -errno;
    }

  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  ret = connect(fd, (FAR struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0 && errno != EINPROGRESS)
    {
      close(fd);
      return -errno;
    }

  if (ret < 0)
    {
      pfd.fd = fd;
      pfd.events = POLLOUT;
      ret = poll(&pfd, 1, timeout_ms);
      if (ret <= 0)
        {
          close(fd);
          return (ret == 0) ? -ETIMEDOUT : -errno;
        }

      errlen = sizeof(err);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (FAR void *)&err,
                     &errlen) < 0 || err != 0)
        {
          close(fd);
          return (err != 0) ? -err : -errno;
        }
    }

  fcntl(fd, F_SETFL, flags);

  /* Apply a receive timeout so ha_ws_recv_message() can poll the command
   * queue between network events.
   */

  {
    struct timeval tv;

    tv.tv_sec = WS_RECV_SLICE_MS / 1000;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv.tv_sec = 5;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  }

  return fd;
}

static void ws_handshake_key(FAR char *key, size_t keylen)
{
  uint8_t raw[16];
  struct timespec ts;
  int i;

  clock_gettime(CLOCK_REALTIME, &ts);
  srand((unsigned)(ts.tv_nsec ^ ts.tv_sec ^ (uintptr_t)key));

  for (i = 0; i < (int)sizeof(raw); i++)
    {
      raw[i] = (uint8_t)rand();
    }

  ha_b64_encode(raw, sizeof(raw), key, keylen);
}

static int ws_expect_accept(FAR const char *clientkey,
                            FAR const char *response)
{
  struct ha_sha1_s sha;
  uint8_t digest[20];
  char expected[32];
  FAR const char *p;
  size_t keylen;

  keylen = strlen(clientkey) + sizeof(WS_GUID);
  {
    FAR char *input = malloc(keylen);

    if (input == NULL)
      {
        return -ENOMEM;
      }

    strcpy(input, clientkey);
    strcat(input, WS_GUID);

    ha_sha1_init(&sha);
    ha_sha1_update(&sha, (FAR const uint8_t *)input, strlen(input));
    ha_sha1_final(&sha, digest);
    free(input);
  }

  ha_b64_encode(digest, sizeof(digest), expected, sizeof(expected));

  p = strstr(response, "Sec-WebSocket-Accept:");
  if (p == NULL)
    {
      p = strstr(response, "sec-websocket-accept:");
      if (p == NULL)
        {
          return -EPROTO;
        }
    }

  p += strlen("Sec-WebSocket-Accept:");
  while (*p == ' ')
    {
      p++;
    }

  if (strncasecmp(p, expected, strlen(expected)) != 0)
    {
      return -EPROTO;
    }

  return 0;
}

static void ws_save_hold(FAR struct ha_ws_s *ws, FAR const uint8_t *data,
                         size_t len)
{
  if (len == 0)
    {
      return;
    }

  if (len > sizeof(ws->hold))
    {
      len = sizeof(ws->hold);
    }

  memcpy(ws->hold, data, len);
  ws->holdlen = len;
}

static ssize_t ws_fill_scratch(FAR struct ha_ws_s *ws)
{
  if (ws->holdlen > 0)
    {
      size_t n = ws->holdlen;

      memcpy(ws->scratch, ws->hold, n);
      ws->holdlen = 0;
      return (ssize_t)n;
    }

  return recv(ws->sockfd, ws->scratch, sizeof(ws->scratch), 0);
}

static void ws_on_new_frame(FAR struct ha_ws_s *ws)
{
  uint8_t opcode = ws->dec.opcode;

  ws->pinglen = 0;

  if (opcode == HA_WS_OP_TEXT)
    {
      ws->msglen = 0;
      ws->discarding = false;
      ws->msg_active = true;
    }
  else if (opcode == HA_WS_OP_BINARY)
    {
      ws->msglen = 0;
      ws->discarding = true;
      ws->msg_active = true;
    }
  else if (opcode == HA_WS_OP_CONT && !ws->msg_active)
    {
      ws->discarding = true;
    }
}

static void ws_handle_payload(FAR struct ha_ws_s *ws, uint8_t opcode,
                              FAR const uint8_t *data, size_t len)
{
  if (len == 0)
    {
      return;
    }

  if (opcode == HA_WS_OP_PING)
    {
      size_t room = sizeof(ws->pingbuf) - ws->pinglen;

      if (len > room)
        {
          len = room;
        }

      memcpy(ws->pingbuf + ws->pinglen, data, len);
      ws->pinglen += len;
      return;
    }

  if (opcode != HA_WS_OP_TEXT && opcode != HA_WS_OP_CONT &&
      opcode != HA_WS_OP_BINARY)
    {
      return;
    }

  if (ws->discarding)
    {
      return;
    }

  if (ws->msglen + len > ws->msgcap)
    {
      ws->discarding = true;
      ws->msglen = 0;
      return;
    }

  memcpy(ws->msg + ws->msglen, data, len);
  ws->msglen += len;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ha_ws_connect(FAR struct ha_ws_s *ws, FAR const char *host,
                  uint16_t port, FAR const char *path, size_t rxmax,
                  long timeout_ms)
{
  char request[512];
  char clientkey[32];
  char response[WS_MAX_HTTP_RESPONSE];
  size_t resplen = 0;
  uint64_t deadline;
  int ret;

  memset(ws, 0, sizeof(*ws));
  ws->sockfd = -1;

  ws->msg = malloc(rxmax + 1);
  if (ws->msg == NULL)
    {
      return -ENOMEM;
    }

  ws->msgcap = rxmax;

  if (path == NULL || path[0] == '\0')
    {
      path = "/";
    }

  ws->sockfd = ws_tcp_connect(host, port, timeout_ms);
  if (ws->sockfd < 0)
    {
      ret = ws->sockfd;
      goto errout;
    }

  ws_handshake_key(clientkey, sizeof(clientkey));
  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\n"
           "Host: %s:%u\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: %s\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "User-Agent: ha_panel/1.0 (openvela)\r\n"
           "\r\n",
           path, host, (unsigned)port, clientkey);

  ret = ws_send_all(ws, (FAR const uint8_t *)request, strlen(request));
  if (ret < 0)
    {
      goto errout;
    }

  /* Read the HTTP handshake response up to the blank line */

  deadline = mono_ms() + WS_HANDSHAKE_TIMEOUT_MS;
  while (resplen < sizeof(response) - 1)
    {
      ssize_t r;

      if (mono_ms() >= deadline)
        {
          ret = -ETIMEDOUT;
          goto errout;
        }

      r = recv(ws->sockfd, response + resplen,
               sizeof(response) - 1 - resplen, 0);
      if (r < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            {
              continue;
            }

          ret = -errno;
          goto errout;
        }

      if (r == 0)
        {
          ret = -ECONNRESET;
          goto errout;
        }

      resplen += r;
      response[resplen] = '\0';

      if (strstr(response, "\r\n\r\n") != NULL)
        {
          break;
        }
    }

  if (strncmp(response, "HTTP/1.1 101", 12) != 0 &&
      strncmp(response, "HTTP/1.0 101", 12) != 0)
    {
      ret = -EPROTO;
      goto errout;
    }

  ret = ws_expect_accept(clientkey, response);
  if (ret < 0)
    {
      goto errout;
    }

  ha_ws_decoder_init(&ws->dec);
  ws->holdlen = 0;
  return 0;

errout:
  if (ws->sockfd >= 0)
    {
      close(ws->sockfd);
      ws->sockfd = -1;
    }

  free(ws->msg);
  ws->msg = NULL;
  return ret;
}

void ha_ws_close(FAR struct ha_ws_s *ws)
{
  if (ws == NULL)
    {
      return;
    }

  if (ws->sockfd >= 0)
    {
      uint8_t payload[2] = { 0x03, 0xe8 }; /* 1000 normal closure */

      ws_send_frame(ws, HA_WS_OP_CLOSE, payload, sizeof(payload));
      close(ws->sockfd);
      ws->sockfd = -1;
    }

  free(ws->msg);
  ws->msg = NULL;
  ws->msglen = 0;
  ws->msgcap = 0;
  ws->header_ready = false;
  ws->discarding = false;
  ws->msg_active = false;
  ws->holdlen = 0;
}

int ha_ws_send_text(FAR struct ha_ws_s *ws, FAR const char *payload)
{
  size_t len = strlen(payload);
  uint8_t key[4];
  FAR uint8_t *frame;
  size_t hdrlen;
  size_t framelen;
  int ret;

  if (ws->sockfd < 0)
    {
      return -ENOTCONN;
    }

  hdrlen = (len < 126) ? 6 : ((len <= 0xffff) ? 8 : 14);

  frame = malloc(hdrlen + len);
  if (frame == NULL)
    {
      return -ENOMEM;
    }

  key[0] = (uint8_t)rand();
  key[1] = (uint8_t)rand();
  key[2] = (uint8_t)rand();
  key[3] = (uint8_t)rand();

  ret = ha_ws_frame_encode(HA_WS_OP_TEXT, (FAR const uint8_t *)payload,
                           len, key, frame, hdrlen + len, &framelen);
  if (ret == 0)
    {
      ret = ws_send_all(ws, frame, framelen);
    }

  free(frame);
  return ret;
}

int ha_ws_recv_message(FAR struct ha_ws_s *ws, FAR char **out,
                       long timeout_ms)
{
  uint64_t deadline = mono_ms() + (uint64_t)timeout_ms;

  *out = NULL;

  for (;;)
    {
      if (!ws->header_ready)
        {
          size_t consumed = 0;
          ssize_t r;
          int dret;

          r = ws_fill_scratch(ws);
          if (r < 0)
            {
              if (errno == EINTR)
                {
                  continue;
                }

              if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                  if (mono_ms() >= deadline)
                    {
                      return -ETIMEDOUT;
                    }

                  continue;
                }

              return -errno;
            }

          if (r == 0)
            {
              return -ECONNRESET;
            }

          dret = ha_ws_header_feed(&ws->dec, ws->scratch, (size_t)r,
                                   &consumed);
          if (dret == HA_WS_DECODE_MORE)
            {
              if (mono_ms() >= deadline)
                {
                  return -ETIMEDOUT;
                }

              continue;
            }

          if (dret == HA_WS_DECODE_ERROR)
            {
              return -EPROTO;
            }

          /* Header complete: bytes at scratch[consumed..r) are payload */

          ws->header_ready = true;
          ws->payload_left = ws->dec.payload_len;
          ws->payload_off = 0;
          ws_on_new_frame(ws);

          if ((size_t)r > consumed)
            {
              size_t plen = (size_t)r - consumed;
              FAR uint8_t *pp = ws->scratch + consumed;

              if ((uint64_t)plen > ws->payload_left)
                {
                  plen = (size_t)ws->payload_left;
                }

              if (ws->dec.masked)
                {
                  ha_ws_unmask(pp, plen, ws->dec.maskkey, ws->payload_off);
                }

              ws->payload_left -= plen;
              ws->payload_off += plen;
              ws_handle_payload(ws, ws->dec.opcode, pp, plen);

              if (ws->payload_left == 0 &&
                  consumed + plen < (size_t)r)
                {
                  ws_save_hold(ws, ws->scratch + consumed + plen,
                               (size_t)r - consumed - plen);
                }
            }

          continue;
        }

      if (ws->payload_left > 0)
        {
          size_t want = sizeof(ws->scratch);
          ssize_t r;

          if ((uint64_t)want > ws->payload_left)
            {
              want = (size_t)ws->payload_left;
            }

          r = recv(ws->sockfd, ws->scratch, want, 0);
          if (r < 0)
            {
              if (errno == EINTR)
                {
                  continue;
                }

              if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                  if (mono_ms() >= deadline)
                    {
                      return -ETIMEDOUT;
                    }

                  continue;
                }

              return -errno;
            }

          if (r == 0)
            {
              return -ECONNRESET;
            }

          if (ws->dec.masked)
            {
              ha_ws_unmask(ws->scratch, (size_t)r, ws->dec.maskkey,
                          ws->payload_off);
            }

          ws->payload_left -= r;
          ws->payload_off += r;
          ws_handle_payload(ws, ws->dec.opcode, ws->scratch, (size_t)r);
          continue;
        }

      /* The current frame is fully consumed */

      ws->header_ready = false;

      if (ws->dec.opcode == HA_WS_OP_PING && !ws->discarding)
        {
          int ret = ws_send_frame(ws, HA_WS_OP_PONG, ws->pingbuf,
                                  ws->pinglen);
          if (ret < 0)
            {
              return ret;
            }

          ha_ws_decoder_init(&ws->dec);
        }
      else if (ws->dec.opcode == HA_WS_OP_CLOSE)
        {
          ws_send_frame(ws, HA_WS_OP_CLOSE, NULL, 0);
          return -ECONNRESET;
        }
      else if (ws->dec.opcode == HA_WS_OP_TEXT ||
               ws->dec.opcode == HA_WS_OP_CONT)
        {
          if (ws->discarding)
            {
              if (ws->dec.fin)
                {
                  ws->discarding = false;
                  ws->msg_active = false;
                  ws->msglen = 0;
                  ha_ws_decoder_init(&ws->dec);
                  return -EMSGSIZE;
                }
            }
          else if (ws->dec.fin)
            {
              ws->msg[ws->msglen] = '\0';
              ws->msg_active = false;
              *out = (FAR char *)ws->msg;
              ha_ws_decoder_init(&ws->dec);
              return 0;
            }

          ha_ws_decoder_init(&ws->dec);
        }
      else
        {
          ha_ws_decoder_init(&ws->dec);
        }
    }
}
