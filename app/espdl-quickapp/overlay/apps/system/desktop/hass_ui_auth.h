/* Generated from the reviewed built-in Home Assistant UI. */
#pragma once
#include "qpk_espdl.h"
#include <string.h>
static const unsigned char g_hass_ui_sha256[32] = {0x7e, 0x15, 0xc5, 0x32, 0x15, 0xf6, 0x27, 0x52, 0x6f, 0x13, 0xbb, 0x8b, 0x91, 0xd6, 0x3b, 0xb4, 0xe3, 0x44, 0xb5, 0xcf, 0x35, 0xc6, 0xdd, 0xfa, 0xd4, 0x64, 0xcf, 0x0e, 0x99, 0xe2, 0x26, 0x34};
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
