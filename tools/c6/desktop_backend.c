/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop_backend.h"
#include "c6net.h"
#include "link_state.h"

static int scan(struct c6_scan_ap *records, size_t capacity, size_t *count)
{
  struct c6_link_snapshot state;
  if (!count) return -EINVAL;
  *count = 0;
  if (!records || !capacity) return -EINVAL;
  int ret = c6net_get_link_snapshot(&state);
  if (ret < 0) return ret;
  if (!state.initialized)
    {
      ret = c6net_prepare();
      if (ret < 0) return ret;
    }
  return esp_hosted_rpc_wifi_scan_results(records, capacity, count);
}

static int connect_ap(const char *ssid, const char *password)
{
  if (!ssid || !password) return -EINVAL;
  size_t length = strnlen(ssid, 33);
  if (!length || length > 32 || strnlen(password, 65) > 64) return -EINVAL;
  return c6net_connect(ssid, password);
}

const struct c6_desktop_backend g_c6_desktop_backend = {scan, connect_ap};
