/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0xe2, 0x26, 0x87, 0x56, 0xb7, 0x66, 0xd6, 0xf6, 0x5d, 0x30, 0x21, 0x67, 0x41, 0x4a, 0xac, 0x6a, 0x2a, 0x20, 0x91, 0x2f, 0xd0, 0xca, 0x92, 0x73, 0x5e, 0x83, 0xe9, 0x4f, 0x9e, 0x0b, 0xce, 0x44};
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
