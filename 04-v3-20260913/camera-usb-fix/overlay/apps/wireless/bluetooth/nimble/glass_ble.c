/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_ble.h"
#include "glass_ble_name.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_register.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct glass_ble_state g_state;
static uint8_t g_address_type;
static uint16_t g_connection;
static struct ble_npl_event g_command_event;
static int g_command;
static bool g_cancel_scan;
static ble_addr_t g_peer;
extern void ble_hci_sock_ack_handler(void *arg);
extern void ble_store_ram_init(void);

void glass_ble_read(struct glass_ble_state *state)
{
  pthread_mutex_lock(&g_lock);
  *state = g_state;
  pthread_mutex_unlock(&g_lock);
}

/* Sort once discovery ends, keeping list order stable during the scan.
 * Named devices come first; within each group show stronger signals first.
 */
static void sort_devices(void)
{
  for (unsigned i = 1; i < g_state.count; i++)
    {
      struct glass_ble_device device = g_state.devices[i];
      unsigned j = i;
      while (j > 0)
        {
          const struct glass_ble_device *previous = &g_state.devices[j - 1];
          bool named = device.name[0] != 0;
          bool previous_named = previous->name[0] != 0;
          if (!(named && !previous_named) &&
              !(named == previous_named && device.rssi > previous->rssi)) break;
          g_state.devices[j] = *previous;
          j--;
        }
      g_state.devices[j] = device;
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
  (void)arg;
  pthread_mutex_lock(&g_lock);
  switch (event->type)
    {
      case BLE_GAP_EVENT_DISC:
        if (g_state.scanning)
          {
            unsigned i;
            for (i = 0; i < g_state.count; i++)
              if (g_state.devices[i].type == event->disc.addr.type &&
                  !memcmp(g_state.devices[i].address, event->disc.addr.val, 6)) break;
            if (i < GLASS_BLE_LIMIT)
              {
                if (i == g_state.count) g_state.count++;
                g_state.devices[i].type = event->disc.addr.type;
                memcpy(g_state.devices[i].address, event->disc.addr.val, 6);
                g_state.devices[i].rssi = event->disc.rssi;
                glass_ble_update_name(&g_state.devices[i],
                                      event->disc.data, event->disc.length_data);
              }
          }
        break;
      case BLE_GAP_EVENT_DISC_COMPLETE:
        sort_devices();
        g_state.scanning = false;
        break;
      case BLE_GAP_EVENT_CONNECT:
        g_state.connecting = false;
        g_state.connected = event->connect.status == 0;
        g_state.error = event->connect.status;
        if (g_state.connected) g_connection = event->connect.conn_handle;
        break;
      case BLE_GAP_EVENT_DISCONNECT:
        g_state.connected = false;
        break;
    }
  pthread_mutex_unlock(&g_lock);
  return 0;
}

/* All GAP commands execute on the host event queue, never under the UI lock. */
static void command_event(struct ble_npl_event *event)
{
  (void)event;
  pthread_mutex_lock(&g_lock);
  int command = g_command;
  ble_addr_t peer = g_peer;
  uint16_t connection = g_connection;
  pthread_mutex_unlock(&g_lock);
  int ret = 0;
  if (command == 1)
    {
      struct ble_gap_disc_params params = {0};
      params.passive = 0; /* Request names carried in scan responses. */
      params.filter_duplicates = 1;
      ret = ble_gap_disc(g_address_type, 10000, &params, gap_event, NULL);
    }
  else if (command == 2)
    ret = ble_gap_connect(g_address_type, &peer, 10000, NULL, gap_event, NULL);
  else if (command == 3)
    ret = ble_gap_disc_cancel();
  else if (command == 4)
    ret = ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
  pthread_mutex_lock(&g_lock);
  if (ret)
    {
      g_state.error = ret;
      if (command == 1) g_state.scanning = false;
      if (command == 2) g_state.connecting = false;
    }
  if (command == 3 && !ret) g_state.scanning = false;
  bool cancel = g_cancel_scan && g_state.scanning;
  g_cancel_scan = false;
  g_command = 0;
  pthread_mutex_unlock(&g_lock);
  if (cancel)
    {
      ret = ble_gap_disc_cancel();
      pthread_mutex_lock(&g_lock);
      if (!ret) g_state.scanning = false;
      else g_state.error = ret;
      pthread_mutex_unlock(&g_lock);
    }
}

static void synced(void)
{
  int ret = ble_hs_util_ensure_addr(0);
  if (!ret) ret = ble_hs_id_infer_auto(0, &g_address_type);
  pthread_mutex_lock(&g_lock);
  g_state.ready = ret == 0;
  g_state.error = ret;
  pthread_mutex_unlock(&g_lock);
}

static void reset(int reason)
{
  pthread_mutex_lock(&g_lock);
  g_state.ready = g_state.scanning = g_state.connecting = g_state.connected = false;
  g_state.error = reason;
  pthread_mutex_unlock(&g_lock);
}

static void *hci_thread(void *arg)
{
  ble_hci_sock_ack_handler(arg);
  return NULL;
}

static int spawn(void *(*entry)(void *))
{
  pthread_attr_t attr;
  pthread_t thread;
  int ret = pthread_attr_init(&attr);
  if (ret) return ret;
  ret = pthread_attr_setstacksize(&attr, 32768);
  if (!ret) ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if (!ret) ret = pthread_create(&thread, &attr, entry, NULL);
  pthread_attr_destroy(&attr);
  return ret;
}

static void *host_thread(void *arg)
{
  (void)arg;
  int ret = c6_ble_register("/dev/ttyHCI0");
  if (!ret)
    {
      nimble_port_init();
      ble_npl_event_init(&g_command_event, command_event, NULL);
      ble_svc_gap_init();
      ble_svc_gatt_init();
      ble_store_ram_init();
      ble_hs_cfg.sync_cb = synced;
      ble_hs_cfg.reset_cb = reset;
      ret = spawn(hci_thread);
      if (!ret) nimble_port_run();
    }
  pthread_mutex_lock(&g_lock);
  g_state.error = ret ? ret : -EIO;
  g_state.ready = false;
  pthread_mutex_unlock(&g_lock);
  return NULL;
}

int glass_ble_start(void)
{
  pthread_mutex_lock(&g_lock);
  int ret = 0;
  if (!g_state.started)
    {
      ret = spawn(host_thread);
      if (!ret) g_state.started = true;
      else g_state.error = -ret;
    }
  pthread_mutex_unlock(&g_lock);
  return -ret;
}

static int submit(int command, unsigned index)
{
  pthread_mutex_lock(&g_lock);
  int ret = 0;
  if (!g_state.ready) ret = -EAGAIN;
  else if (g_command) ret = -EBUSY;
  else if ((command == 1 || command == 2) &&
           (g_state.scanning || g_state.connecting || g_state.connected)) ret = -EBUSY;
  else if (command == 2 && index >= g_state.count) ret = -EINVAL;
  else if (command == 4 && !g_state.connected) ret = -ENOTCONN;
  else
    {
      if (command == 1)
        {
          memset(g_state.devices, 0, sizeof(g_state.devices));
          g_state.count = 0;
          g_state.scanning = true;
        }
      if (command == 2)
        {
          g_state.connecting = true;
          g_peer.type = g_state.devices[index].type;
          memcpy(g_peer.val, g_state.devices[index].address, 6);
        }
      g_command = command;
      g_state.error = 0;
      ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &g_command_event);
    }
  pthread_mutex_unlock(&g_lock);
  return ret;
}
int glass_ble_scan(void) { return submit(1, 0); }
int glass_ble_connect(unsigned index) { return submit(2, index); }
int glass_ble_cancel(void)
{
  pthread_mutex_lock(&g_lock);
  if (g_command == 1)
    {
      g_cancel_scan = true;
      pthread_mutex_unlock(&g_lock);
      return 0;
    }
  bool scanning = g_state.scanning;
  pthread_mutex_unlock(&g_lock);
  return scanning ? submit(3, 0) : 0;
}
int glass_ble_disconnect(void) { return submit(4, 0); }
