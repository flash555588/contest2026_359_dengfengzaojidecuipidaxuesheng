/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_BLE_HOSTED_H
#define C6_BLE_HOSTED_H
#include "ble_driver.h"

/* Owner serializes start/stop. Destroy only after destroying/unregistering the
 * bt_driver and excluding every API caller. No automatic C6 reset or Wi-Fi init.
 */
struct c6_ble_transport *c6_ble_hosted_create(void);
void c6_ble_hosted_destroy(struct c6_ble_transport *transport);
#endif
