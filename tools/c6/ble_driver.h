/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_BLE_DRIVER_H
#define C6_BLE_DRIVER_H
#include <nuttx/wireless/bluetooth/bt_driver.h>

typedef int (*c6_ble_rx_t)(void *arg, uint8_t type,
                          const uint8_t *data, size_t length);

/* Transport owns polling and exclusive HCI registration. start failure must
 * leave no callback installed. stop drains callbacks and in-flight send calls.
 * start/stop are serialized by the stack owner; callbacks must not close the
 * driver synchronously. send consumes/copies its buffer before returning.
 */
struct c6_ble_transport
{
  void *context;
  int (*start)(void *context, c6_ble_rx_t receive, void *arg);
  void (*stop)(void *context);
  int (*send)(void *context, const uint8_t *h4, size_t length);
};

/* Allocation only; caller registers the returned object with bt_driver_register.
 * Destroy only after unregistering and excluding all upper-half callers.
 */
struct bt_driver_s *c6_ble_driver_create(const struct c6_ble_transport *transport);
void c6_ble_driver_destroy(struct bt_driver_s *driver);
#endif
