/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0xbc, 0x79, 0x36, 0xd2, 0xfb, 0x42, 0xbf, 0x73, 0xdb, 0xa3, 0xd8, 0xed, 0xf2, 0xfc, 0x75, 0x7d, 0x88, 0x44, 0x40, 0xef, 0xa2, 0xec, 0xf1, 0xed, 0x83, 0xdc, 0x34, 0xee, 0xba, 0x07, 0x1e, 0xc7};
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
