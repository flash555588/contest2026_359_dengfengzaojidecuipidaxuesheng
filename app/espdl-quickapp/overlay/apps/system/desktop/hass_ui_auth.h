/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0xd7, 0x57, 0x5a, 0x96, 0xb5, 0x9d, 0xc6, 0x03, 0x23, 0x1d, 0xef, 0x9e, 0x73, 0xdd, 0x06, 0xd8, 0xe6, 0x13, 0x6d, 0x6b, 0xed, 0x93, 0xa3, 0x93, 0x62, 0xde, 0xef, 0xea, 0x5b, 0xab, 0xf3, 0xde};
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
