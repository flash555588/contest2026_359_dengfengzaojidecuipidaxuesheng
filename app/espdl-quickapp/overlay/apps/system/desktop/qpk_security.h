/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline bool qpk_reserved_package(const char *package)
{
  return package != NULL && strcmp(package, "com.openvela.homeassistant") == 0;
}

/* filename is supplied by native launchers, never by a manifest. External
 * filenames are constructed under QPK_DIR, not in this builtin namespace.
 */
static inline bool qpk_builtin_ha_origin(const char *package, const char *filename)
{
  return qpk_reserved_package(package) && filename != NULL &&
         strcmp(filename, "builtin:/homeassistant/app.js") == 0;
}

static inline bool qpk_launch_identity_valid(const char *package,
                                            const char *filename, size_t capacity)
{
  if (package != NULL && strlen(package) >= capacity) return false;
  return !qpk_reserved_package(package) || qpk_builtin_ha_origin(package, filename);
}
