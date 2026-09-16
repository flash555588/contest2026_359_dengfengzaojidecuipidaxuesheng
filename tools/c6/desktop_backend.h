/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_DESKTOP_BACKEND_H
#define C6_DESKTOP_BACKEND_H
#include "desktop_worker.h"

/* Scan prepares the radio without requesting association. Neither callback implies IP
 * configuration or Internet reachability. Use from the worker, never LVGL.
 */
extern const struct c6_desktop_backend g_c6_desktop_backend;
#endif
