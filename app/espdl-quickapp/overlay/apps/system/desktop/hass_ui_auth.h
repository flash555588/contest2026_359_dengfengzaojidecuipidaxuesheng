/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0x34, 0xa0, 0x70, 0x7e, 0xf2, 0xb2, 0x2a, 0x83, 0xc7, 0xbd, 0x4d, 0x1b, 0x8a, 0x6f, 0xa3, 0xb6, 0xea, 0x9d, 0xd2, 0xf9, 0xc8, 0x9b, 0xa8, 0xbb, 0xcb, 0x8d, 0x23, 0xfb, 0x0c, 0x45, 0x5b, 0xa5};
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
