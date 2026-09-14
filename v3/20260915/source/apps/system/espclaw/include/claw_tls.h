/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CLAW_TLS_H
#define CLAW_TLS_H
#include <stddef.h>
/* CA PEM is public trust material, not a client private key. It must remain
 * alive for all requests. Call once before workers; no persistent writes. */
int claw_tls_initialize(const unsigned char *ca_pem, size_t ca_size);
#endif
