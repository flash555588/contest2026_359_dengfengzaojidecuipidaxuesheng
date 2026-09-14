/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define GLASS_BLE_LIMIT 12
struct glass_ble_device
{
  uint8_t type;
  uint8_t address[6];
  int rssi;
};
struct glass_ble_state
{
  bool started, ready, scanning, connecting, connected;
  int error;
  unsigned count;
  struct glass_ble_device devices[GLASS_BLE_LIMIT];
};
int glass_ble_start(void);
int glass_ble_scan(void);
int glass_ble_connect(unsigned index);
int glass_ble_cancel(void);
int glass_ble_disconnect(void);
void glass_ble_read(struct glass_ble_state *state);
