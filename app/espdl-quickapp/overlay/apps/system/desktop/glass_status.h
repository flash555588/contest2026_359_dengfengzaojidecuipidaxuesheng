/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>

#define GLASS_STATUS_HEIGHT 32
#define GLASS_CONTENT_HEIGHT (600 - GLASS_STATUS_HEIGHT)

/* Link plus IPv4 lease, not a claim of Internet reachability. */
struct glass_status_network
{
  bool wifi, wired, joining;
};
