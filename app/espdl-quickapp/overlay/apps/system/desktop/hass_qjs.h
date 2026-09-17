/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <quickjs.h>

/* Call on the UI thread. Only trusted native code selects the grant mask.
 * Installs system.homeAssistantService; GC/context teardown detaches this client.
 */
int hass_qjs_install(JSContext *context, JSValue system, unsigned grants);
