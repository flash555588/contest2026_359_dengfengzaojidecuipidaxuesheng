/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0xe5, 0x71, 0xfb, 0x13, 0x35, 0x3a, 0x4a, 0xa8, 0x7c, 0xb5, 0x98, 0x0b, 0x76, 0xd1, 0x04, 0x2d, 0x5d, 0x4b, 0x0b, 0x5c, 0x68, 0xaa, 0x0f, 0xaa, 0x35, 0x93, 0x5a, 0xa7, 0x66, 0x32, 0x84, 0x50};
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
