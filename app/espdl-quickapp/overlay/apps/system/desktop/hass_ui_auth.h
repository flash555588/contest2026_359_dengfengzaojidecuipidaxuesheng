/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0x12, 0x8d, 0xaf, 0xf4, 0x9e, 0x91, 0xa6, 0xcb, 0xb9, 0x34, 0xe5, 0xfb, 0xd6, 0x93, 0xee, 0xc4, 0x87, 0xfb, 0x38, 0x08, 0x5a, 0x2f, 0x62, 0xf7, 0x93, 0x33, 0x8b, 0x64, 0x30, 0x9c, 0x91, 0xdd};
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
