/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CLAW_CONNECT_H
#define CLAW_CONNECT_H
/* Returns a connected blocking socket, or negative errno. Caller owns fd.
 * DNS is blocking; timeout applies to the address connection attempts. */
int claw_connect_tcp(const char *host, const char *port, unsigned int timeout_ms);
#endif
