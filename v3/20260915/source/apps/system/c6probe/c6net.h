/****************************************************************************
 * apps/system/c6probe/c6net.h
 ****************************************************************************/

#ifndef __APPS_SYSTEM_C6PROBE_C6NET_H
#define __APPS_SYSTEM_C6PROBE_C6NET_H

#include <nuttx/config.h>
#include <errno.h>
#include <stdbool.h>
#include <nuttx/compiler.h>

#if defined(CONFIG_NET) && defined(CONFIG_SYSTEM_C6PROBE)
int c6net_initialize(FAR const char *ssid, FAR const char *password);
int c6net_connect(FAR const char *ssid, FAR const char *password);
int c6net_disconnect(void);
int c6net_prepare(void);
struct c6_link_snapshot;
int c6net_get_link_snapshot(struct c6_link_snapshot *out);
bool c6net_is_initialized(void);
bool c6net_is_associated(void);
#else
static inline int c6net_initialize(FAR const char *ssid,
  FAR const char *password)
{
  (void)ssid;
  (void)password;
  return -ENOSYS;
}

static inline int c6net_connect(FAR const char *ssid,
  FAR const char *password)
{
  (void)ssid;
  (void)password;
  return -ENOSYS;
}

static inline bool c6net_is_initialized(void)
{
  return false;
}

static inline bool c6net_is_associated(void)
{
  return false;
}
#endif

#endif
