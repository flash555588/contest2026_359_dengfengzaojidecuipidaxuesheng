/****************************************************************************
 * apps/ha_panel/ha_config.c
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ha_config.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static FAR char *trim(FAR char *s)
{
  FAR char *e;

  while (*s != '\0' && isspace((unsigned char)*s))
    {
      s++;
    }

  if (*s == '\0')
    {
      return s;
    }

  e = s + strlen(s) - 1;
  while (e > s && isspace((unsigned char)*e))
    {
      *e-- = '\0';
    }

  return s;
}

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

static int parse_line(FAR struct ha_config_s *cfg, FAR char *line)
{
  FAR char *eq;
  FAR char *key;
  FAR char *val;

  line = trim(line);
  if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
    {
      return 0;
    }

  eq = strchr(line, '=');
  if (eq == NULL)
    {
      return -EINVAL;
    }

  *eq = '\0';
  key = trim(line);
  val = trim(eq + 1);

  if (strcmp(key, "server") == 0 || strcmp(key, "host") == 0)
    {
      cpy(cfg->server, sizeof(cfg->server), val);
    }
  else if (strcmp(key, "port") == 0)
    {
      long p = strtol(val, NULL, 10);

      if (p <= 0 || p > 65535)
        {
          return -EINVAL;
        }

      cfg->port = (uint16_t)p;
    }
  else if (strcmp(key, "token") == 0)
    {
      cpy(cfg->token, sizeof(cfg->token), val);
    }
  else
    {
      /* Ignore unknown keys so future fields stay compatible. */
    }

  return 0;
}

static int load_path(FAR struct ha_config_s *cfg, FAR const char *path)
{
  FILE *fp;
  char line[640];
  int ret = 0;

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      return -errno;
    }

  while (fgets(line, sizeof(line), fp) != NULL)
    {
      ret = parse_line(cfg, line);
      if (ret < 0)
        {
          break;
        }
    }

  fclose(fp);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void ha_config_init(FAR struct ha_config_s *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  cpy(cfg->server, sizeof(cfg->server), CONFIG_HA_PANEL_SERVER_DEFAULT);
  cfg->port = CONFIG_HA_PANEL_PORT_DEFAULT;
  cpy(cfg->token, sizeof(cfg->token), CONFIG_HA_PANEL_TOKEN_DEFAULT);
  cpy(cfg->path, sizeof(cfg->path), CONFIG_HA_PANEL_CONF_PATH);
}

int ha_config_load(FAR struct ha_config_s *cfg)
{
  int ret;

  ret = load_path(cfg, cfg->path);
  if (ret == 0)
    {
      return 0;
    }

  return load_path(cfg, CONFIG_HA_PANEL_CONF_PATH_ALT);
}

int ha_config_save(FAR const struct ha_config_s *cfg)
{
  FILE *fp;
  char tmp[168];
  int n;

  n = snprintf(tmp, sizeof(tmp), "%s.tmp", cfg->path);
  if (n < 0 || n >= (int)sizeof(tmp))
    {
      return -ENAMETOOLONG;
    }

  fp = fopen(tmp, "w");
  if (fp == NULL)
    {
      fp = fopen(cfg->path, "w");
      if (fp == NULL)
        {
          return -errno;
        }

      fprintf(fp, "server=%s\nport=%u\ntoken=%s\n",
              cfg->server, (unsigned)cfg->port, cfg->token);
      fclose(fp);
      return 0;
    }

  fprintf(fp, "server=%s\nport=%u\ntoken=%s\n",
          cfg->server, (unsigned)cfg->port, cfg->token);
  fclose(fp);

  if (rename(tmp, cfg->path) < 0)
    {
      unlink(cfg->path);
      if (rename(tmp, cfg->path) < 0)
        {
          return -errno;
        }
    }

  return 0;
}

void ha_config_usage(FAR const char *prog)
{
  printf("Usage: %s [-s host] [-p port] [-t token] [-f conf]\n"
         "  Home Assistant LVGL panel (ws://host:port/api/websocket)\n"
         "  Long-lived token: HA Profile -> Security -> Long-Lived Access Tokens\n",
         prog);
}

int ha_config_apply_args(FAR struct ha_config_s *cfg, int argc,
                         FAR char *argv[])
{
  int i;

  for (i = 1; i < argc; i++)
    {
      FAR char *opt = argv[i];
      FAR char *val = NULL;

      if (opt[0] != '-')
        {
          ha_config_usage(argv[0]);
          return -EINVAL;
        }

      if ((opt[1] == 's' || opt[1] == 'p' || opt[1] == 't' ||
           opt[1] == 'f') && opt[2] == '\0')
        {
          if (i + 1 >= argc)
            {
              ha_config_usage(argv[0]);
              return -EINVAL;
            }

          val = argv[++i];
        }

      switch (opt[1])
        {
          case 's':
            cpy(cfg->server, sizeof(cfg->server), val);
            break;

          case 'p':
            {
              long p = strtol(val, NULL, 10);

              if (p <= 0 || p > 65535)
                {
                  return -EINVAL;
                }

              cfg->port = (uint16_t)p;
            }
            break;

          case 't':
            cpy(cfg->token, sizeof(cfg->token), val);
            break;

          case 'f':
            cpy(cfg->path, sizeof(cfg->path), val);
            break;

          case 'h':
            ha_config_usage(argv[0]);
            return -EINTR;

          default:
            ha_config_usage(argv[0]);
            return -EINVAL;
        }
    }

  return 0;
}
