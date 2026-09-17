/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_weather.h"
#include "c6net.h"
#include "link_state.h"
#include <netutils/netlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* Hourly weather updates fit the API's anonymous monthly quota. Errors back
 * off; manual refresh is limited to one request per minute. No worker touches
 * LVGL, and TLS/body/parser allocations are released after each request. */
#define WEATHER_REFRESH_MS (60ULL * 60 * 1000)
#define WEATHER_RETRY_MAX_MS (15ULL * 60 * 1000)
static pthread_mutex_t g_weather_lock = PTHREAD_MUTEX_INITIALIZER;
static struct glass_weather_snapshot g_weather;
static bool g_weather_started;
static bool g_weather_refresh;

static uint64_t now_ms(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool online(struct c6_link_snapshot *link)
{
  struct in_addr address;
  return c6net_get_link_snapshot(link) == 0 && link->associated && link->carrier_ready &&
         netlib_get_ipv4addr("eth0", &address) == 0 && address.s_addr != 0;
}

static void set_state(enum glass_weather_state state, int error)
{
  pthread_mutex_lock(&g_weather_lock);
  if (g_weather.state != state || g_weather.error != error)
    {
      g_weather.state = state;
      g_weather.error = error;
      g_weather.revision++;
    }
  pthread_mutex_unlock(&g_weather_lock);
}

static void *weather_worker(void *arg)
{
  (void)arg;
  pthread_setname_np(pthread_self(), "weather");
  uint64_t next = 0, last_attempt = 0, generation = 0;
  uint64_t backoff = 60000;
  bool was_online = false;
  for (;;)
    {
      struct c6_link_snapshot link;
      if (!online(&link))
        {
          was_online = false;
          set_state(WEATHER_WAIT_NET, 0);
          sleep(2);
          continue;
        }
      if (time(NULL) < 1735689600)
        {
          set_state(WEATHER_WAIT_TIME, 0);
          sleep(2);
          continue;
        }
      uint64_t now = now_ms();
      pthread_mutex_lock(&g_weather_lock);
      bool requested = g_weather_refresh;
      pthread_mutex_unlock(&g_weather_lock);
      if (!was_online || generation != link.generation || requested)
        {
          uint64_t earliest = last_attempt ? last_attempt + 60000 : 0;
          if (next > earliest) next = earliest;
        }
      was_online = true;
      generation = link.generation;
      if (now < next) { sleep(2); continue; }
      pthread_mutex_lock(&g_weather_lock);
      g_weather_refresh = false;
      pthread_mutex_unlock(&g_weather_lock);
      set_state(WEATHER_FETCHING, 0);
      last_attempt = now;
      struct glass_weather_data data;
      int ret = glass_weather_fetch(&data);
      struct c6_link_snapshot after;
      if (!online(&after) || after.generation != generation) ret = -ENETDOWN;
      now = now_ms();
      if (ret == 0)
        {
          pthread_mutex_lock(&g_weather_lock);
          g_weather.data = data;
          g_weather.received_ms = now;
          g_weather.valid = true;
          g_weather.state = WEATHER_READY;
          g_weather.error = 0;
          g_weather.revision++;
          pthread_mutex_unlock(&g_weather_lock);
          backoff = 60000;
          next = now + WEATHER_REFRESH_MS;
          printf("[weather] updated city=%s AQI=%d temp=%d humidity=%d\n",
                 data.location, data.aqi, data.temperature, data.humidity);
        }
      else
        {
          set_state(WEATHER_FAILED, ret);
          if (ret == -EAGAIN) backoff = WEATHER_RETRY_MAX_MS;
          next = now + backoff;
          printf("[weather] request failed %d; retry in %u seconds\n", ret,
                 (unsigned)(backoff / 1000));
          backoff = backoff * 2 > WEATHER_RETRY_MAX_MS ? WEATHER_RETRY_MAX_MS : backoff * 2;
        }
      sleep(2);
    }
  return NULL;
}

void glass_weather_get(struct glass_weather_snapshot *out)
{
  pthread_mutex_lock(&g_weather_lock);
  *out = g_weather;
  pthread_mutex_unlock(&g_weather_lock);
}

void glass_weather_refresh(void)
{
  pthread_mutex_lock(&g_weather_lock);
  g_weather_refresh = true;
  pthread_mutex_unlock(&g_weather_lock);
}

void glass_weather_start(void)
{
  pthread_mutex_lock(&g_weather_lock);
  if (g_weather_started) { pthread_mutex_unlock(&g_weather_lock); return; }
  g_weather_started = true;
  pthread_mutex_unlock(&g_weather_lock);
  pthread_attr_t attr;
  pthread_t thread;
  int ret = pthread_attr_init(&attr);
  if (!ret)
    {
      ret = pthread_attr_setstacksize(&attr, 24576);
      if (!ret) ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      if (!ret) ret = pthread_create(&thread, &attr, weather_worker, NULL);
      pthread_attr_destroy(&attr);
    }
  if (ret)
    {
      pthread_mutex_lock(&g_weather_lock);
      g_weather_started = false;
      pthread_mutex_unlock(&g_weather_lock);
      set_state(WEATHER_FAILED, -ret);
      printf("[weather] cannot create worker: %d\n", ret);
    }
}
