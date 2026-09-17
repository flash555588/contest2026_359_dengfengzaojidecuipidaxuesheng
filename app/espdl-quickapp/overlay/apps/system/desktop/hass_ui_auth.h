/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0x91, 0x9f, 0x54, 0x19, 0x2c, 0x51, 0x71, 0x0a, 0x9b, 0xa1, 0xa2, 0xd4, 0xcf, 0x62, 0x75, 0x59, 0xb4, 0x1e, 0x1d, 0x4e, 0x02, 0x47, 0xf6, 0xae, 0xd5, 0xb9, 0xd1, 0xa1, 0x6a, 0xb3, 0x87, 0x00};
static unsigned hass_ui_grants(const char *package, const char *source, size_t source_len)
{
  if (package == NULL || source == NULL ||
      strcmp(package, "com.openvela.homeassistant") != 0) return 0;
  unsigned char digest[32];
  qpk_dl_sha256(source, source_len, digest);
  unsigned difference = 0;
  for (unsigned i = 0; i < sizeof(digest); i++)
    difference |= digest[i] ^ g_hass_ui_sha256[i];
  memset(digest, 0, sizeof(digest));
  return difference == 0 ? HASS_READ | HASS_CONTROL | HASS_CONFIGURE : 0;
}
