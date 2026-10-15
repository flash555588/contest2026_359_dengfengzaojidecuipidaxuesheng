/* SPDX-License-Identifier: Apache-2.0 */
#include "hass_portal.h"
#include "glass_portal.h"
#include <errno.h>

int hass_config_persist(const char *url, const char *token)
{
  if (!url || !token) return -EINVAL;
  return portal_ha_save(url, token);
}

int hass_portal_bootstrap(void)
{
  /* Read and apply under the store lock; app entry cannot overwrite a newer
   * remote save with an older bootstrap snapshot. */
  return portal_ha_apply();
}
