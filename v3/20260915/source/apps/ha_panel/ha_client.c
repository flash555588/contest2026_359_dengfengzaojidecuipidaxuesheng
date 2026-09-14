/****************************************************************************
 * apps/ha_panel/ha_client.c
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
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "netutils/cJSON.h"

#include "ha_client.h"
#include "ha_ws.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HA_CMD_Q        16
#define HA_EVT_Q        16
#define HA_PING_MS      30000
#define HA_RECV_MS      400
#define HA_BACKOFF_MAX  30

#ifndef CONFIG_HA_PANEL_NET_PRIORITY
#  define CONFIG_HA_PANEL_NET_PRIORITY 100
#endif

#ifndef CONFIG_HA_PANEL_NET_STACKSIZE
#  define CONFIG_HA_PANEL_NET_STACKSIZE 16384
#endif

#ifndef CONFIG_HA_PANEL_WS_RX_MAX
#  define CONFIG_HA_PANEL_WS_RX_MAX 262144
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum ha_cmd_type_e
{
  HA_CMD_STOP = 0,
  HA_CMD_RECONNECT,
  HA_CMD_SERVICE
};

struct ha_cmd_s
{
  enum ha_cmd_type_e type;
  char domain[24];
  char service[24];
  char entity_id[64];
  char extra[128];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct ha_config_s *g_cfg;
static FAR struct ha_store_s *g_store;
static volatile bool g_stop;
static volatile enum ha_conn_e g_conn = HA_CONN_IDLE;
static int g_pid = -1;

static sem_t g_cmd_lock;
static sem_t g_evt_lock;
static int g_cmd_r;
static int g_cmd_w;
static int g_cmd_n;
static int g_evt_r;
static int g_evt_w;
static int g_evt_n;
static struct ha_cmd_s g_cmds[HA_CMD_Q];
static struct ha_evt_s g_evts[HA_EVT_Q];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t mono_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void q_lock(FAR sem_t *s)
{
  while (sem_wait(s) < 0 && errno == EINTR)
    {
    }
}

static int cmd_push(FAR const struct ha_cmd_s *cmd)
{
  q_lock(&g_cmd_lock);
  if (g_cmd_n >= HA_CMD_Q)
    {
      sem_post(&g_cmd_lock);
      return -ENOSPC;
    }

  g_cmds[g_cmd_w] = *cmd;
  g_cmd_w = (g_cmd_w + 1) % HA_CMD_Q;
  g_cmd_n++;
  sem_post(&g_cmd_lock);
  return 0;
}

static int cmd_pop(FAR struct ha_cmd_s *cmd)
{
  q_lock(&g_cmd_lock);
  if (g_cmd_n == 0)
    {
      sem_post(&g_cmd_lock);
      return -EAGAIN;
    }

  *cmd = g_cmds[g_cmd_r];
  g_cmd_r = (g_cmd_r + 1) % HA_CMD_Q;
  g_cmd_n--;
  sem_post(&g_cmd_lock);
  return 0;
}

static void evt_push(enum ha_evt_type_e type, enum ha_conn_e conn,
                     FAR const char *text)
{
  q_lock(&g_evt_lock);
  if (g_evt_n >= HA_EVT_Q)
    {
      /* Drop the oldest event so the UI still sees a recent snapshot. */

      g_evt_r = (g_evt_r + 1) % HA_EVT_Q;
      g_evt_n--;
    }

  g_evts[g_evt_w].type = type;
  g_evts[g_evt_w].conn = conn;
  g_evts[g_evt_w].text[0] = '\0';
  if (text != NULL)
    {
      strncpy(g_evts[g_evt_w].text, text, sizeof(g_evts[g_evt_w].text) - 1);
      g_evts[g_evt_w].text[sizeof(g_evts[g_evt_w].text) - 1] = '\0';
    }

  g_evt_w = (g_evt_w + 1) % HA_EVT_Q;
  g_evt_n++;
  sem_post(&g_evt_lock);
}

static void set_conn(enum ha_conn_e conn, FAR const char *text)
{
  g_conn = conn;
  evt_push(HA_EVT_CONN, conn, text);
}

static int send_json(FAR struct ha_ws_s *ws, FAR const char *json)
{
  return ha_ws_send_text(ws, json);
}

static int handle_json(FAR const char *json)
{
  FAR cJSON *root;
  FAR cJSON *type;
  FAR const char *t;
  int changed = 0;

  root = cJSON_Parse(json);
  if (root == NULL)
    {
      return -EPROTO;
    }

  type = cJSON_GetObjectItemCaseSensitive(root, "type");
  t = (cJSON_IsString(type) && type->valuestring) ? type->valuestring : "";

  if (strcmp(t, "auth_required") == 0)
    {
      changed = 1;
    }
  else if (strcmp(t, "auth_ok") == 0)
    {
      FAR const char *ver;
      FAR cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "ha_version");

      ver = (cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
      ha_store_lock(g_store);
      strncpy(g_store->ha_version, ver, sizeof(g_store->ha_version) - 1);
      g_store->ha_version[sizeof(g_store->ha_version) - 1] = '\0';
      ha_store_unlock(g_store);
      changed = 2;
    }
  else if (strcmp(t, "auth_invalid") == 0)
    {
      changed = -EPERM;
    }
  else if (strcmp(t, "result") == 0)
    {
      FAR cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "success");
      FAR cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");

      if (cJSON_IsTrue(ok) && cJSON_IsArray(result))
        {
          FAR cJSON *item;

          ha_store_clear(g_store);
          cJSON_ArrayForEach(item, result)
            {
              ha_store_ingest_state(g_store, item);
            }

          evt_push(HA_EVT_STORE, g_conn, "states");
        }
      else if (cJSON_IsFalse(ok))
        {
          FAR cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
          FAR cJSON *msg = err ? cJSON_GetObjectItemCaseSensitive(err,
                                                                  "message")
                               : NULL;
          evt_push(HA_EVT_ERROR, g_conn,
                   (cJSON_IsString(msg) && msg->valuestring) ?
                   msg->valuestring : "service failed");
        }
    }
  else if (strcmp(t, "event") == 0)
    {
      FAR cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
      FAR cJSON *data;
      FAR cJSON *ns;

      data = event ? cJSON_GetObjectItemCaseSensitive(event, "data") : NULL;
      ns = data ? cJSON_GetObjectItemCaseSensitive(data, "new_state") : NULL;
      if (ns != NULL && !cJSON_IsNull(ns))
        {
          if (ha_store_ingest_state(g_store, ns) > 0)
            {
              evt_push(HA_EVT_STORE, g_conn, "delta");
            }
        }
    }

  cJSON_Delete(root);
  return changed;
}

static int session_auth(FAR struct ha_ws_s *ws)
{
  char buf[640];
  FAR char *msg;
  int ret;
  int n;

  ret = ha_ws_recv_message(ws, &msg, 8000);
  if (ret < 0)
    {
      return ret;
    }

  ret = handle_json(msg);
  if (ret != 1)
    {
      return -EPROTO;
    }

  n = snprintf(buf, sizeof(buf),
               "{\"type\":\"auth\",\"access_token\":\"%s\"}", g_cfg->token);
  if (n < 0 || n >= (int)sizeof(buf))
    {
      return -ENOSPC;
    }

  ret = send_json(ws, buf);
  if (ret < 0)
    {
      return ret;
    }

  ret = ha_ws_recv_message(ws, &msg, 8000);
  if (ret < 0)
    {
      return ret;
    }

  ret = handle_json(msg);
  if (ret == -EPERM)
    {
      return -EPERM;
    }

  if (ret != 2)
    {
      return -EPROTO;
    }

  return 0;
}

static int session_setup(FAR struct ha_ws_s *ws, FAR int *msgid)
{
  char buf[128];
  int n;

  n = snprintf(buf, sizeof(buf),
               "{\"id\":%d,\"type\":\"subscribe_events\","
               "\"event_type\":\"state_changed\"}", *msgid);
  if (n < 0 || n >= (int)sizeof(buf))
    {
      return -ENOSPC;
    }

  if (send_json(ws, buf) < 0)
    {
      return -EIO;
    }

  (*msgid)++;

  n = snprintf(buf, sizeof(buf), "{\"id\":%d,\"type\":\"get_states\"}",
               *msgid);
  if (n < 0 || n >= (int)sizeof(buf))
    {
      return -ENOSPC;
    }

  if (send_json(ws, buf) < 0)
    {
      return -EIO;
    }

  (*msgid)++;
  return 0;
}

static int send_service(FAR struct ha_ws_s *ws, FAR int *msgid,
                        FAR const struct ha_cmd_s *cmd)
{
  char buf[768];
  int n;

  if (cmd->extra[0] != '\0')
    {
      n = snprintf(buf, sizeof(buf),
                   "{\"id\":%d,\"type\":\"call_service\",\"domain\":\"%s\","
                   "\"service\":\"%s\",\"service_data\":{\"entity_id\":\"%s\",%s}}",
                   *msgid, cmd->domain, cmd->service, cmd->entity_id,
                   cmd->extra);
    }
  else
    {
      n = snprintf(buf, sizeof(buf),
                   "{\"id\":%d,\"type\":\"call_service\",\"domain\":\"%s\","
                   "\"service\":\"%s\",\"service_data\":{\"entity_id\":\"%s\"}}",
                   *msgid, cmd->domain, cmd->service, cmd->entity_id);
    }

  if (n < 0 || n >= (int)sizeof(buf))
    {
      return -ENOSPC;
    }

  (*msgid)++;
  return send_json(ws, buf);
}

static int drain_cmds(FAR struct ha_ws_s *ws, FAR int *msgid,
                      FAR bool *reconnect)
{
  struct ha_cmd_s cmd;
  int ret = 0;

  while (cmd_pop(&cmd) == 0)
    {
      if (cmd.type == HA_CMD_STOP)
        {
          g_stop = true;
          return 0;
        }

      if (cmd.type == HA_CMD_RECONNECT)
        {
          *reconnect = true;
          return 0;
        }

      if (cmd.type == HA_CMD_SERVICE)
        {
          ha_store_apply_service(g_store, cmd.domain, cmd.service,
                                 cmd.entity_id,
                                 cmd.extra[0] != '\0' ? cmd.extra : NULL);
          evt_push(HA_EVT_STORE, g_conn, cmd.entity_id);

          if (ws != NULL && msgid != NULL)
            {
              ret = send_service(ws, msgid, &cmd);
              if (ret < 0)
                {
                  return ret;
                }
            }
        }
    }

  return 0;
}

static int session_loop(FAR struct ha_ws_s *ws)
{
  int msgid = 1;
  uint64_t last_ping = mono_ms();
  bool reconnect = false;
  int ret;

  ret = session_setup(ws, &msgid);
  if (ret < 0)
    {
      return ret;
    }

  set_conn(HA_CONN_LIVE, "live");

  while (!g_stop && !reconnect)
    {
      FAR char *msg;
      char pbuf[48];

      ret = ha_ws_recv_message(ws, &msg, HA_RECV_MS);
      if (ret == 0)
        {
          handle_json(msg);
        }
      else if (ret == -EMSGSIZE)
        {
          evt_push(HA_EVT_ERROR, g_conn, "message too large");
        }
      else if (ret == -ETIMEDOUT)
        {
          /* Fall through to command drain / ping. */
        }
      else
        {
          return ret;
        }

      ret = drain_cmds(ws, &msgid, &reconnect);
      if (ret < 0)
        {
          return ret;
        }

      if (mono_ms() - last_ping >= HA_PING_MS)
        {
          snprintf(pbuf, sizeof(pbuf), "{\"id\":%d,\"type\":\"ping\"}",
                   msgid++);
          send_json(ws, pbuf);
          last_ping = mono_ms();
        }
    }

  return reconnect ? 1 : 0;
}

static int ha_client_task(int argc, FAR char *argv[])
{
  int backoff = 1;

  UNUSED(argc);
  UNUSED(argv);

  while (!g_stop)
    {
      struct ha_ws_s ws;
      bool reconnect = false;
      int ret;

      drain_cmds(NULL, NULL, &reconnect);

      if (g_cfg->token[0] == '\0')
        {
          ha_store_load_demo(g_store);
          set_conn(HA_CONN_DEMO, "demo mode (no token)");
          evt_push(HA_EVT_STORE, HA_CONN_DEMO, "demo");
          do
            {
              sleep(1);
              drain_cmds(NULL, NULL, &reconnect);
            }
          while (!g_stop && g_cfg->token[0] == '\0' && !reconnect);

          continue;
        }

      set_conn(HA_CONN_CONNECTING, "connecting");
      memset(&ws, 0, sizeof(ws));
      ret = ha_ws_connect(&ws, g_cfg->server, g_cfg->port,
                          "/api/websocket", CONFIG_HA_PANEL_WS_RX_MAX,
                          8000);
      if (ret < 0)
        {
          char err[64];

          snprintf(err, sizeof(err), "connect failed (%d)", ret);
          set_conn(HA_CONN_ERROR, err);
          sleep(backoff);
          if (backoff < HA_BACKOFF_MAX)
            {
              backoff *= 2;
              if (backoff > HA_BACKOFF_MAX)
                {
                  backoff = HA_BACKOFF_MAX;
                }
            }

          continue;
        }

      set_conn(HA_CONN_AUTH, "authenticating");
      ret = session_auth(&ws);
      if (ret == -EPERM)
        {
          set_conn(HA_CONN_ERROR, "auth failed");
          ha_ws_close(&ws);
          sleep(5);
          continue;
        }

      if (ret < 0)
        {
          char err[64];

          snprintf(err, sizeof(err), "handshake failed (%d)", ret);
          set_conn(HA_CONN_ERROR, err);
          ha_ws_close(&ws);
          sleep(backoff);
          continue;
        }

      backoff = 1;
      ret = session_loop(&ws);
      ha_ws_close(&ws);

      if (g_stop)
        {
          break;
        }

      set_conn(HA_CONN_ERROR, "disconnected");
      if (ret != 1)
        {
          sleep(backoff);
          if (backoff < HA_BACKOFF_MAX)
            {
              backoff *= 2;
            }
        }
    }

  set_conn(HA_CONN_IDLE, "stopped");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ha_client_start(FAR struct ha_config_s *cfg,
                    FAR struct ha_store_s *store)
{
  g_cfg = cfg;
  g_store = store;
  g_stop = false;
  g_cmd_r = g_cmd_w = g_cmd_n = 0;
  g_evt_r = g_evt_w = g_evt_n = 0;
  sem_init(&g_cmd_lock, 0, 1);
  sem_init(&g_evt_lock, 0, 1);

  g_pid = task_create("ha_net", CONFIG_HA_PANEL_NET_PRIORITY,
                      CONFIG_HA_PANEL_NET_STACKSIZE, ha_client_task, NULL);
  if (g_pid < 0)
    {
      return -errno;
    }

  return 0;
}

void ha_client_stop(void)
{
  struct ha_cmd_s cmd;

  memset(&cmd, 0, sizeof(cmd));
  cmd.type = HA_CMD_STOP;
  g_stop = true;
  cmd_push(&cmd);
}

void ha_client_reconnect(void)
{
  struct ha_cmd_s cmd;

  memset(&cmd, 0, sizeof(cmd));
  cmd.type = HA_CMD_RECONNECT;
  cmd_push(&cmd);
}

int ha_client_call(FAR const char *domain, FAR const char *service,
                   FAR const char *entity_id, FAR const char *extra)
{
  struct ha_cmd_s cmd;

  memset(&cmd, 0, sizeof(cmd));
  cmd.type = HA_CMD_SERVICE;
  strncpy(cmd.domain, domain, sizeof(cmd.domain) - 1);
  strncpy(cmd.service, service, sizeof(cmd.service) - 1);
  strncpy(cmd.entity_id, entity_id, sizeof(cmd.entity_id) - 1);
  if (extra != NULL)
    {
      strncpy(cmd.extra, extra, sizeof(cmd.extra) - 1);
    }

  return cmd_push(&cmd);
}

int ha_client_poll_evt(FAR struct ha_evt_s *evt)
{
  q_lock(&g_evt_lock);
  if (g_evt_n == 0)
    {
      sem_post(&g_evt_lock);
      return -EAGAIN;
    }

  *evt = g_evts[g_evt_r];
  g_evt_r = (g_evt_r + 1) % HA_EVT_Q;
  g_evt_n--;
  sem_post(&g_evt_lock);
  return 0;
}

enum ha_conn_e ha_client_conn(void)
{
  return g_conn;
}
