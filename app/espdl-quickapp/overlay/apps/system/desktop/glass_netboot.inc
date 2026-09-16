/* Bring the Wi-Fi link up at boot.
 *
 * The companion radio is only a bridge: the host owns eth0 and IPv4, so a
 * lease has to be requested after the station associates. The slave keeps the
 * credentials of the last successful join in its own NVS, which is what makes
 * the boot-time association possible without storing a password on the host.
 */
#ifdef CONFIG_SYSTEM_C6_DESKTOP
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netutils/netlib.h>
#ifdef CONFIG_NETUTILS_NTPCLIENT
#  include <netutils/ntpclient.h>
#endif
#include "c6net.h"
#include "link_state.h"

#define GLASS_NET_IFNAME "eth0"
#define GLASS_NET_LINK_WAIT_MS 12000
/* The companion-radio bring-up and the DHCP client run on these threads and
 * need the same stack budget as the C6 desktop worker. A smaller stack
 * overflows silently and corrupts the heap. */
#ifndef CONFIG_SYSTEM_C6_DESKTOP_STACKSIZE
#  define GLASS_NET_STACKSIZE 8192
#else
#  define GLASS_NET_STACKSIZE CONFIG_SYSTEM_C6_DESKTOP_STACKSIZE
#endif

static pthread_mutex_t g_net_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_net_notice[96];
static bool g_net_notice_pending;
static bool g_net_lease_running;
static bool g_net_time_started;

/* The system clock has to be right for HTTPS, and the desktop only shows a
 * date once it is. The daemon keeps retrying, so starting it once is enough. */
static void glass_net_time_sync(void)
{
#ifdef CONFIG_NETUTILS_NTPCLIENT
  if (g_net_time_started) return;
  g_net_time_started = true;
  printf("[net] ntp start: %d\n", ntpc_start());
#endif
}

/* Messages are handed to the LVGL thread instead of touching the desktop from
 * the network worker. */
static void glass_net_post(const char *text)
{
  pthread_mutex_lock(&g_net_lock);
  snprintf(g_net_notice, sizeof(g_net_notice), "%s", text);
  g_net_notice_pending = true;
  pthread_mutex_unlock(&g_net_lock);
}

static const char *glass_net_take_notice(void)
{
  static char text[sizeof(g_net_notice)];

  pthread_mutex_lock(&g_net_lock);
  if (!g_net_notice_pending)
    {
      pthread_mutex_unlock(&g_net_lock);
      return NULL;
    }

  strlcpy(text, g_net_notice, sizeof(text));
  g_net_notice_pending = false;
  pthread_mutex_unlock(&g_net_lock);
  return text;
}

static bool glass_net_associated(void)
{
  struct c6_link_snapshot state;
  return c6net_get_link_snapshot(&state) == 0 && state.associated;
}

static bool glass_net_address(char *text, size_t length)
{
  struct in_addr address;

  if (netlib_get_ipv4addr(GLASS_NET_IFNAME, &address) < 0 || address.s_addr == 0)
    {
      return false;
    }

  snprintf(text, length, "%u.%u.%u.%u",
           (unsigned)(address.s_addr & 0xff),
           (unsigned)((address.s_addr >> 8) & 0xff),
           (unsigned)((address.s_addr >> 16) & 0xff),
           (unsigned)((address.s_addr >> 24) & 0xff));
  return true;
}

/* One lease attempt at a time: the settings page and the boot task share it. */
static void *glass_net_lease_thread(void *arg)
{
  bool announce = arg != NULL;
  char address[24];
  int ret = -ETIMEDOUT;

  /* A connect request returns before the station is associated, so wait for
   * the link instead of racing it. */
  for (int waited = 0; waited < GLASS_NET_LINK_WAIT_MS && !glass_net_associated();
       waited += 500)
    {
      usleep(500000);
    }

  for (int attempt = 0; attempt < 3; attempt++)
    {
      ret = netlib_obtain_ipv4addr(GLASS_NET_IFNAME);
      if (ret == 0) break;
      sleep(2);
    }

  pthread_mutex_lock(&g_net_lock);
  g_net_lease_running = false;
  pthread_mutex_unlock(&g_net_lock);

  if (ret < 0 || !glass_net_address(address, sizeof(address)))
    {
      printf("[net] lease failed: %d\n", ret);
      if (announce) glass_net_post("已连接 Wi-Fi，但未取得 IP 地址");
      return NULL;
    }

  printf("[net] online %s\n", address);
  glass_net_time_sync();
  if (announce)
    {
      char text[sizeof(g_net_notice)];
      snprintf(text, sizeof(text), "已连接 Wi-Fi · %s", address);
      glass_net_post(text);
    }
  return NULL;
}

static void glass_net_lease(bool announce)
{
  pthread_t thread;
  pthread_attr_t attr;

  pthread_mutex_lock(&g_net_lock);
  if (g_net_lease_running)
    {
      pthread_mutex_unlock(&g_net_lock);
      return;
    }

  g_net_lease_running = true;
  pthread_mutex_unlock(&g_net_lock);

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, GLASS_NET_STACKSIZE);
  if (pthread_create(&thread, &attr, glass_net_lease_thread,
                     announce ? (void *)1 : NULL) != 0)
    {
      g_net_lease_running = false;
      pthread_attr_destroy(&attr);
      glass_net_lease_thread(announce ? (void *)1 : NULL);
      return;
    }

  pthread_attr_destroy(&attr);
  pthread_detach(thread);
}

static void *glass_net_boot_thread(void *arg)
{
  int ret = -1;

  (void)arg;
  for (int attempt = 0; attempt < 4; attempt++)
    {
      ret = c6net_prepare();
      if (ret == 0) break;
      printf("[net] companion radio attempt %d: %d\n", attempt + 1, ret);
      sleep(2);
    }

  if (ret < 0)
    {
      printf("[net] companion radio unavailable: %d\n", ret);
      return NULL;
    }

  /* Starting the station already reconnects with the stored credentials;
     ask explicitly once in case the slave came up idle. */
  bool associated = false;
  for (int waited = 0; waited < GLASS_NET_LINK_WAIT_MS && !associated; waited += 500)
    {
      associated = glass_net_associated();
      if (!associated) usleep(500000);
    }

  if (!associated && c6net_connect("", "") == 0)
    {
      for (int waited = 0; waited < 6000 && !associated; waited += 500)
        {
          associated = glass_net_associated();
          if (!associated) usleep(500000);
        }
    }

  if (!associated)
    {
      printf("[net] no saved access point\n");
      return NULL;
    }

  printf("[net] associated, requesting a lease\n");
  glass_net_lease(true);
  return NULL;
}

static void glass_net_autostart(void)
{
  pthread_t thread;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, GLASS_NET_STACKSIZE);
  if (pthread_create(&thread, &attr, glass_net_boot_thread, NULL) != 0)
    {
      pthread_attr_destroy(&attr);
      printf("[net] cannot start the network worker\n");
      return;
    }

  pthread_attr_destroy(&attr);
  pthread_detach(thread);
}
#endif /* CONFIG_SYSTEM_C6_DESKTOP */
