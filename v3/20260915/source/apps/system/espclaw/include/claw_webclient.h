/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CLAW_WEBCLIENT_H
#define CLAW_WEBCLIENT_H
#include <netutils/webclient.h>
/* Configure once before starting workers. Connector must validate the trust
 * chain AND hostname and honor bounded I/O timeouts. Owned by application. */
int claw_webclient_set_tls(const struct webclient_tls_ops *ops, void *context);
#endif
