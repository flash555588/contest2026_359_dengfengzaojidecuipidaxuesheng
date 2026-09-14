/* SPDX-License-Identifier: Apache-2.0 */
#include "ble_driver.h"
#include "ble_h4.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

/* Conservative bounded ACL payload for the initial BLE transport. */
#define C6_BLE_PACKET_MAX 1028
struct c6_ble_driver_s
{
  struct bt_driver_s driver;
  struct c6_ble_transport transport;
  pthread_mutex_t txlock;
  bool opened;
  uint8_t tx[C6_BLE_PACKET_MAX + 1];
};

static int receive_packet(void *arg, uint8_t type,
                          const uint8_t *data, size_t length)
{
  struct c6_ble_driver_s *priv = arg;
  if (length > C6_BLE_PACKET_MAX) return -EMSGSIZE;
  int ret = c6_h4_receive(type, data, length);
  if (ret < 0) return ret;
  if (!priv->driver.receive) return -ENODEV;
  return bt_netdev_receive(&priv->driver,
                          type == C6_H4_EVENT ? BT_EVT : BT_ACL_IN,
                          (void *)data, length);
}

static int open_driver(struct bt_driver_s *driver)
{
  struct c6_ble_driver_s *priv = (struct c6_ble_driver_s *)driver;
  if (priv->opened) return 0;
  int ret = priv->transport.start(priv->transport.context, receive_packet, priv);
  if (ret < 0) return ret;
  priv->opened = true;
  return 0;
}

static int send_packet(struct bt_driver_s *driver, enum bt_buf_type_e type,
                       void *data, size_t length)
{
  struct c6_ble_driver_s *priv = (struct c6_ble_driver_s *)driver;
  if (length > C6_BLE_PACKET_MAX) return -EMSGSIZE;
  uint8_t h4;
  if (type == BT_CMD) h4 = C6_H4_COMMAND;
  else if (type == BT_ACL_OUT) h4 = C6_H4_ACL;
  else return -EPROTONOSUPPORT;
  int ret = pthread_mutex_lock(&priv->txlock);
  if (ret) return -ret;
  int packed = c6_h4_pack(h4, data, length, priv->tx, sizeof(priv->tx));
  if (packed < 0) ret = packed;
  else
    {
      ret = priv->transport.send(priv->transport.context, priv->tx, packed);
      if (ret == 0) ret = (int)length;
      else if (ret > 0) ret = -EIO;
    }
  pthread_mutex_unlock(&priv->txlock);
  return ret;
}

static void close_driver(struct bt_driver_s *driver)
{
  struct c6_ble_driver_s *priv = (struct c6_ble_driver_s *)driver;
  if (!priv->opened) return;
  priv->transport.stop(priv->transport.context);
  priv->opened = false;
}

struct bt_driver_s *c6_ble_driver_create(const struct c6_ble_transport *transport)
{
  if (!transport || !transport->start || !transport->stop || !transport->send)
    { errno = EINVAL; return NULL; }
  struct c6_ble_driver_s *priv = calloc(1, sizeof(*priv));
  if (!priv) return NULL;
  int ret = pthread_mutex_init(&priv->txlock, NULL);
  if (ret) { free(priv); errno = ret; return NULL; }
  priv->transport = *transport;
  priv->driver.open = open_driver;
  priv->driver.close = close_driver;
  priv->driver.send = send_packet;
  /* uart_bth4 uses driver.priv for its own upper-half state. */
  return &priv->driver;
}

void c6_ble_driver_destroy(struct bt_driver_s *driver)
{
  if (!driver) return;
  struct c6_ble_driver_s *priv = (struct c6_ble_driver_s *)driver;
  close_driver(driver);
  pthread_mutex_destroy(&priv->txlock);
  free(priv);
}
