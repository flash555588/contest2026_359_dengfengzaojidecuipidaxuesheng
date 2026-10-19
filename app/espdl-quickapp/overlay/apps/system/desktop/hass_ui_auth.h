/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0xc8, 0xbe, 0x67, 0x6a, 0xb9, 0x48, 0x5a, 0xfe, 0x2e, 0xaf, 0x7f, 0x04, 0x06, 0xca, 0x1f, 0x23, 0x19, 0xea, 0x1c, 0x7b, 0x36, 0xa0, 0x8b, 0x13, 0x3a, 0x9a, 0x8f, 0xeb, 0x0b, 0xb4, 0xf7, 0x4d};
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
