/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* UI thread only. Published by the authenticated built-in HA application.
 * At most two device shortcuts; this API never submits device commands. */
int glass_home_devices_publish(const char *json);
