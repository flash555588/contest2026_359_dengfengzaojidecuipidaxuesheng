/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0xf9, 0x33, 0x7c, 0x70, 0x6d, 0x47, 0x39, 0x5d, 0x91, 0x6d, 0x52, 0x74, 0x9f, 0x7d, 0x53, 0x6f, 0xb4, 0x4a, 0xe0, 0xe2, 0xa4, 0x25, 0x71, 0x23, 0xac, 0xe9, 0x20, 0xeb, 0x91, 0xf5, 0xa2, 0x4a};
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
