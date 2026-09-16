/* SPDX-License-Identifier: Apache-2.0
 * ESPClaw selects its trust bundle; all TCP/TLS I/O uses the verified shared
 * desktop HTTPS transport. Do not add another TLS implementation here.
 */
#include "claw_tls.h"
#include "claw_webclient.h"
#include "glass_https.h"
#include <errno.h>

int claw_tls_initialize(const unsigned char *ca_pem,size_t ca_size)
{
  static struct glass_https_request defaults;
  if(defaults.ca_pem) return -EALREADY;
  int ret=glass_https_validate_ca(ca_pem,ca_size);
  if(ret) return ret;
  defaults.ca_pem=ca_pem; defaults.ca_size=ca_size;
  return claw_webclient_set_tls(glass_https_tls_ops(),&defaults);
}
