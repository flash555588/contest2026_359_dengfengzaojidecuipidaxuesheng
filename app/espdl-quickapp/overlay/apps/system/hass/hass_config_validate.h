/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <string.h>

#define HASS_URL_CAPACITY 128
#define HASS_TOKEN_CAPACITY 512

/* Match the native HTTP transport. Never save an endpoint it cannot use. */
static inline bool hass_config_url_valid(const char *url)
{
  if (!url || strlen(url) >= HASS_URL_CAPACITY || strncmp(url, "http://", 7)) return false;
  const char *p = url + 7;
  size_t n = strcspn(p, ":/");
  if (!n) return false;
  for (size_t i = 0; i < n; i++)
    if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') ||
          (p[i] >= '0' && p[i] <= '9') || p[i] == '.' || p[i] == '-' || p[i] == '_')) return false;
  p += n;
  if (*p == ':')
    {
      unsigned port = 0;
      const char *start = ++p;
      while (*p >= '0' && *p <= '9')
        { port = port * 10 + (*p++ - '0'); if (port > 65535) return false; }
      if (p == start || !port) return false;
    }
  return !*p || !strcmp(p, "/");
}

static inline bool hass_config_token_valid(const char *token)
{
  if (!token || strlen(token) >= HASS_TOKEN_CAPACITY) return false;
  for (const unsigned char *p = (const unsigned char *)token; *p; p++)
    if (*p < 33 || *p > 126) return false;
  return true;
}
