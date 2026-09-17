/* SPDX-License-Identifier: Apache-2.0 */
#include "hass_portal.h"
#include "hass_service.h"
#include "glass_portal.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>

static void wipe(void *data, size_t size)
{
  volatile unsigned char *p = data;
  while (size--)
    {
      *p++ = 0;
    }
}

int hass_config_persist(const char *url, const char *token)
{
  if (url == NULL || token == NULL)
    {
      return -EINVAL;
    }

  return portal_ha_save(url, token);
}

int hass_portal_bootstrap(void)
{
  char url[128] = {0};
  char token[512] = {0};
  int ret_url = portal_ha_value("ha_url", url, sizeof(url));
  int ret_token = portal_ha_value("ha_token", token, sizeof(token));
  int ret = 0;

  if (ret_url == 0 && ret_token == 0 && url[0] != '\0' && token[0] != '\0')
    {
      int client = hass_open(HASS_READ | HASS_CONTROL | HASS_CONFIGURE);
      if (client < 0)
        {
          ret = client;
        }
      else
        {
          ret = hass_configure((uint32_t)client, url, token, true);
          hass_close((uint32_t)client);
        }
    }
  else if ((ret_url != 0 && ret_url != -ENOENT) ||
           (ret_token != 0 && ret_token != -ENOENT))
    {
      ret = ret_url != 0 && ret_url != -ENOENT ? ret_url : ret_token;
    }

  wipe(token, sizeof(token));
  wipe(url, sizeof(url));
  return ret;
}
