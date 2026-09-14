/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "glass_settings_service.h"
#include "glass_settings_store.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

static pthread_mutex_t g_preferences_lock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
  bool active;
  bool pending;
  uint8_t latest[4];
  int error;
} g_preferences_work;

static void *glass_preferences_worker(void *unused)
{
  (void)unused;
  uint8_t saved[4];
  /* Only this worker touches the store header's generation and slot state.
   * Reload on each start to continue the latest valid on-disk generation.
   */
  bool have_saved = glass_settings_load(saved) == 0;
  pthread_mutex_lock(&g_preferences_lock);
  if (g_preferences_work.error) have_saved = false;
  pthread_mutex_unlock(&g_preferences_lock);
  for (;;)
    {
      pthread_mutex_lock(&g_preferences_lock);
      if (!g_preferences_work.pending)
        {
          g_preferences_work.active = false;
          pthread_mutex_unlock(&g_preferences_lock);
          return NULL;
        }
      pthread_mutex_unlock(&g_preferences_lock);

      struct timespec delay = {.tv_nsec = 250000000};
      while (nanosleep(&delay, &delay) < 0)
        {
          if (errno != EINTR)
            {
              int error = errno ? errno : EIO;
              pthread_mutex_lock(&g_preferences_lock);
              g_preferences_work.error = error;
              g_preferences_work.pending = false;
              g_preferences_work.active = false;
              pthread_mutex_unlock(&g_preferences_lock);
              return NULL;
            }
        }

      uint8_t values[4];
      pthread_mutex_lock(&g_preferences_lock);
      memcpy(values, g_preferences_work.latest, sizeof(values));
      g_preferences_work.pending = false;
      pthread_mutex_unlock(&g_preferences_lock);

      int error = 0;
      if (!have_saved || memcmp(values, saved, sizeof(values)))
        {
          errno = 0;
          if (glass_settings_save(values) != 0)
            {
              error = errno ? errno : EIO;
              /* A failed fsync can leave a readable newer record. Do not
               * deduplicate a later request against the older cached value.
               */
              have_saved = false;
            }
          else
            {
              memcpy(saved, values, sizeof(saved));
              have_saved = true;
            }
        }
      pthread_mutex_lock(&g_preferences_lock);
      g_preferences_work.error = error;
      pthread_mutex_unlock(&g_preferences_lock);
    }
}

int glass_preferences_submit(const uint8_t values[4])
{
  if (!values || values[0] > 1 || values[1] > 2 || values[2] > 2 || values[3] > 1)
    return EINVAL;
  pthread_mutex_lock(&g_preferences_lock);
  memcpy(g_preferences_work.latest, values, sizeof(g_preferences_work.latest));
  g_preferences_work.pending = true;
  if (g_preferences_work.active)
    {
      pthread_mutex_unlock(&g_preferences_lock);
      return 0;
    }

  pthread_attr_t attributes;
  int error = pthread_attr_init(&attributes);
  if (!error)
    {
#ifdef __NuttX__
      size_t stack = 8192;
#else
      size_t stack = 64 * 1024;
#endif
#ifdef PTHREAD_STACK_MIN
      if (stack < (size_t)PTHREAD_STACK_MIN) stack = PTHREAD_STACK_MIN;
#endif
      error = pthread_attr_setstacksize(&attributes, stack);
      if (!error) error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
      if (!error)
        {
          pthread_t thread;
          g_preferences_work.active = true;
          error = pthread_create(&thread, &attributes, glass_preferences_worker, NULL);
        }
      pthread_attr_destroy(&attributes);
    }
  if (error)
    {
      g_preferences_work.pending = false;
      g_preferences_work.active = false;
      g_preferences_work.error = error;
    }
  pthread_mutex_unlock(&g_preferences_lock);
  return error;
}

int glass_preferences_get_status(struct glass_preferences_status *status)
{
  if (!status) return EINVAL;
  pthread_mutex_lock(&g_preferences_lock);
  status->pending = g_preferences_work.active || g_preferences_work.pending;
  status->error = g_preferences_work.error;
  pthread_mutex_unlock(&g_preferences_lock);
  return 0;
}
