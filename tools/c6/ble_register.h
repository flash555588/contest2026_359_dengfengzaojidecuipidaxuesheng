/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_BLE_REGISTER_H
#define C6_BLE_REGISTER_H

/* Call once from the application owner after scheduler startup. Successful
 * registration retains driver/transport for the device lifetime. No public
 * unregister is exposed because uart_bth4 provides no matching teardown API.
 * Registration does not initialize/reset C6; prepare Hosted before opening.
 */
int c6_ble_register(const char *path);
#endif
