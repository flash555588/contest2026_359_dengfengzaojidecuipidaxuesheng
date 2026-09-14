/****************************************************************************
 * apps/system/c6probe/c6net.c
 *
 * ESP-Hosted STA Ethernet adapter for the NuttX network stack.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_NET

#include <errno.h>
#include <net/if.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/mutex.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/netconfig.h>

#include "c6net.h"
#include "link_state.h"
#include "esp_hosted.h"

#ifndef CONFIG_NET_ETH_PKTSIZE
#  define CONFIG_NET_ETH_PKTSIZE 1500
#endif

#define C6NET_BUFSIZE (CONFIG_NET_ETH_PKTSIZE + 64)
#define C6NET_POLL_US       10000
#define C6NET_TASK_PRIORITY 100
#define C6NET_TASK_STACK    4096

struct c6net_state_s
{
  struct net_driver_s dev;
  pid_t daemon_pid;
  bool registered;
  uint8_t buf[C6NET_BUFSIZE] __attribute__((aligned(4)));
};

static struct c6net_state_s g_c6net;
/* Singleton companion radio; keep mutex outside the resettable netdev. */
static struct c6_link_state g_c6link = C6_LINK_STATE_INITIALIZER;

int c6net_get_link_snapshot(struct c6_link_snapshot *out)
{
  return c6_link_read(&g_c6link, out);
}

static void c6net_sync_carrier(struct net_driver_s *dev)
{
  struct c6_link_snapshot value;
  netdev_lock(dev);
  if (c6_link_read(&g_c6link, &value) == 0)
    {
      bool ready = value.initialized && value.ifup && value.associated;
      if (ready) netdev_carrier_on(dev);
      else netdev_carrier_off(dev);
      if (c6_link_publish_carrier(&g_c6link, value.generation, ready) < 0)
        netdev_carrier_off(dev);
    }
  else netdev_carrier_off(dev);
  netdev_unlock(dev);
}
static pthread_mutex_t g_c6net_connect_lock = PTHREAD_MUTEX_INITIALIZER;

static int c6net_daemon(int argc, FAR char *argv[])
{
  FAR struct c6net_state_s *priv = &g_c6net;

  UNUSED(argc);
  UNUSED(argv);

  while (c6net_is_initialized())
    {
      c6net_sync_carrier(&priv->dev);

      while (!esp_hosted_rpc_is_waiting() && esp_hosted_poll() > 0)
        {
        }

      usleep(C6NET_POLL_US);
    }

  priv->daemon_pid = -1;
  return 0;
}

static int c6net_txpoll(FAR struct net_driver_s *dev)
{
  if (dev->d_len > 0)
    {
      if (esp_hosted_send(ESP_HOSTED_IF_STA, 0, dev->d_buf,
                          dev->d_len) < 0)
        {
          return -EIO;
        }

      dev->d_len = 0;
    }

  return 0;
}

static int c6net_ifup(FAR struct net_driver_s *dev)
{
  int ret = c6_link_update(&g_c6link, C6_LINK_IFUP);
  if (ret < 0) return ret;
  dev->d_flags |= IFF_UP | IFF_RUNNING;
  return 0;
}

static int c6net_ifdown(FAR struct net_driver_s *dev)
{
  int ret = c6_link_update(&g_c6link, C6_LINK_IFDOWN);
  dev->d_flags &= ~(IFF_UP | IFF_RUNNING);
  netdev_carrier_off(dev);
  return ret;
}

static int c6net_txavail(FAR struct net_driver_s *dev)
{
  if ((dev->d_flags & IFF_UP) != 0)
    {
      netdev_lock(dev);
      devif_poll(dev, c6net_txpoll);
      netdev_unlock(dev);
    }

  return 0;
}

static void c6net_wifi_event(FAR void *arg, bool connected)
{
  UNUSED(arg);
  (void)c6_link_update(&g_c6link, connected ? C6_LINK_ASSOCIATED :
                                           C6_LINK_DISCONNECTED);
}

static void c6net_rx(FAR void *arg, uint8_t if_num,
                     FAR const uint8_t *payload, uint16_t len,
                     uint8_t pkt_type)
{
  FAR struct c6net_state_s *priv = arg;
  uint16_t ethertype;

  UNUSED(if_num);
  UNUSED(pkt_type);

  struct c6_link_snapshot value;
  if (c6_link_read(&g_c6link, &value) < 0 || !value.ifup ||
      len < 14 || len > sizeof(priv->buf))
    {
      return;
    }

  netdev_lock(&priv->dev);
  memcpy(priv->buf, payload, len);
  priv->dev.d_buf = priv->buf;
  priv->dev.d_len = len;

  ethertype = ((uint16_t)priv->buf[12] << 8) | priv->buf[13];
  if (ethertype == ETHTYPE_IP)
    {
      ipv4_input(&priv->dev);
    }
#ifdef CONFIG_NET_IPv6
  else if (ethertype == ETHTYPE_IPV6)
    {
      ipv6_input(&priv->dev);
    }
#endif
#ifdef CONFIG_NET_ARP
  else if (ethertype == ETHTYPE_ARP)
    {
      arp_input(&priv->dev);
    }
#endif

  /* NuttX returns protocol responses such as TCP ACKs and ARP replies in
   * d_buf/d_len.  Ethernet drivers must transmit that response before
   * releasing the network lock; dropping it stalls multi-segment streams. */

  if (priv->dev.d_len > 0)
    {
      (void)c6net_txpoll(&priv->dev);
    }

  netdev_unlock(&priv->dev);
}

static int c6net_setup(FAR const char *ssid, FAR const char *password,
                       bool associate)
{
  FAR struct c6net_state_s *priv = &g_c6net;
  uint8_t mac[6];
  int lockret;
  int ret;

  lockret = pthread_mutex_lock(&g_c6net_connect_lock);
  if (lockret != 0)
    {
      return -lockret;
    }

  if (c6net_is_initialized())
    {
      if (!associate) { ret = 0; goto out; }
      ret = c6_link_update(&g_c6link, C6_LINK_DISCONNECTED);
      if (ret < 0) goto out;
      ret = esp_hosted_rpc_wifi_connect(ssid, password);
      goto out;
    }

  if (priv->registered) goto start_daemon;

  ret = esp_hosted_initialize(false);
  if (ret < 0)
    {
      goto out;
    }

  ret = esp_hosted_rpc_wifi_init();
  if (ret < 0)
    {
      goto out;
    }

  ret = esp_hosted_rpc_wifi_set_mode(1);
  if (ret < 0)
    {
      goto out;
    }

  ret = esp_hosted_rpc_wifi_start();
  if (ret < 0)
    {
      goto out;
    }

  ret = esp_hosted_rpc_get_mac(0, mac);
  if (ret < 0)
    {
      goto out;
    }

  memset(priv, 0, sizeof(*priv));
  memcpy(priv->dev.d_mac.ether.ether_addr_octet, mac, 6);
  priv->dev.d_ifname[0] = 'e';
  priv->dev.d_ifname[1] = 't';
  priv->dev.d_ifname[2] = 'h';
  priv->dev.d_ifname[3] = '0';
  priv->dev.d_llhdrlen = 14;
  priv->dev.d_pktsize = CONFIG_NET_ETH_PKTSIZE;
  priv->dev.d_buf = priv->buf;
  priv->dev.d_ifup = c6net_ifup;
  priv->dev.d_ifdown = c6net_ifdown;
  priv->dev.d_txavail = c6net_txavail;
  priv->dev.d_private = priv;

  ret = esp_hosted_rpc_set_wifi_event_cb(c6net_wifi_event, priv);
  if (ret < 0)
    {
      goto out;
    }

  ret = esp_hosted_register(ESP_HOSTED_IF_STA, c6net_rx, priv);
  if (ret < 0)
    {
      goto out;
    }

  ret = netdev_register(&priv->dev, NET_LL_ETHERNET);
  if (ret < 0)
    {
      goto out;
    }

  priv->registered = true;

start_daemon:
  ret = c6_link_update(&g_c6link, C6_LINK_STARTED);
  if (ret < 0) goto out;
  netdev_lock(&priv->dev);
  ret = c6net_ifup(&priv->dev);
  netdev_unlock(&priv->dev);
  if (ret < 0)
    {
      (void)c6_link_update(&g_c6link, C6_LINK_STOPPED);
      goto out;
    }

  priv->daemon_pid = task_create("c6net", C6NET_TASK_PRIORITY,
                                 C6NET_TASK_STACK, c6net_daemon, NULL);
  if (priv->daemon_pid < 0)
    {
      ret = -errno;
      (void)c6_link_update(&g_c6link, C6_LINK_STOPPED);
      netdev_lock(&priv->dev);
      c6net_ifdown(&priv->dev);
      netdev_unlock(&priv->dev);
      goto out;
    }

  /* An empty SSID asks the C6 to reconnect with the credentials persisted in
   * its own NVS.  A failed association does not tear down eth0, so Settings
   * can provide new credentials without resetting the transport. */

  ret = associate ? esp_hosted_rpc_wifi_connect(ssid, password) : 0;

out:
  pthread_mutex_unlock(&g_c6net_connect_lock);
  return ret;
}


int c6net_disconnect(void)
{
  int ret = pthread_mutex_lock(&g_c6net_connect_lock);
  if (ret != 0) return -ret;
  if (!c6net_is_initialized()) ret = -ENETDOWN;
  else
    {
      ret = esp_hosted_rpc_wifi_disconnect();
      if (ret == 0) ret = c6_link_update(&g_c6link, C6_LINK_DISCONNECTED);
    }
  pthread_mutex_unlock(&g_c6net_connect_lock);
  return ret;
}

int c6net_prepare(void)
{
  return c6net_setup(NULL, NULL, false);
}

int c6net_initialize(FAR const char *ssid, FAR const char *password)
{
  return c6net_setup(ssid, password, true);
}

int c6net_connect(FAR const char *ssid, FAR const char *password)
{
  return c6net_initialize(ssid, password);
}

bool c6net_is_initialized(void)
{
  struct c6_link_snapshot value;
  return c6_link_read(&g_c6link, &value) == 0 && value.initialized;
}

bool c6net_is_associated(void)
{
  struct c6_link_snapshot value;
  return c6_link_read(&g_c6link, &value) == 0 && value.initialized &&
         value.ifup && value.associated && value.carrier_ready;
}

#endif /* CONFIG_NET */
