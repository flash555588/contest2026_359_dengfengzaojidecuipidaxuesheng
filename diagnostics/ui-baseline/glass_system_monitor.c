/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "glass_system_monitor.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct glass_system_monitor
{
  pthread_mutex_t mutex;
  pthread_cond_t wake;
  unsigned references;
  bool cancel;
  struct glass_system_status status;
};

static pthread_mutex_t g_system_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_system_active;

bool glass_system_busy(void)
{
  pthread_mutex_lock(&g_system_lock);
  bool active = g_system_active;
  pthread_mutex_unlock(&g_system_lock);
  return active;
}

static void glass_system_unref(struct glass_system_monitor *monitor)
{
  pthread_mutex_lock(&monitor->mutex);
  bool destroy = --monitor->references == 0;
  pthread_mutex_unlock(&monitor->mutex);
  if (destroy)
    {
      pthread_cond_destroy(&monitor->wake);
      pthread_mutex_destroy(&monitor->mutex);
      free(monitor);
    }
}

static void *glass_system_worker(void *argument)
{
  struct glass_system_monitor *monitor = argument;
  for (;;)
    {
      pthread_mutex_lock(&monitor->mutex);
      bool cancelled = monitor->cancel;
      pthread_mutex_unlock(&monitor->mutex);
      if (cancelled) break;

      struct glass_system_sample sample;
      glass_system_sample_native(&sample);
      pthread_mutex_lock(&monitor->mutex);
      if (monitor->cancel)
        {
          pthread_mutex_unlock(&monitor->mutex);
          break;
        }
      monitor->status.sample = sample;
      monitor->status.ready = true;
      struct timespec deadline;
      if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0)
        {
          monitor->status.error = errno ? errno : EIO;
          pthread_mutex_unlock(&monitor->mutex);
          break;
        }
      deadline.tv_sec++;
      while (!monitor->cancel)
        {
          int error = pthread_cond_timedwait(&monitor->wake, &monitor->mutex,
                                             &deadline);
          if (error == ETIMEDOUT) break;
          if (error)
            {
              monitor->status.error = error;
              monitor->cancel = true;
            }
        }
      pthread_mutex_unlock(&monitor->mutex);
    }
  pthread_mutex_lock(&monitor->mutex);
  monitor->status.stopped = true;
  pthread_mutex_unlock(&monitor->mutex);
  glass_system_unref(monitor);
  pthread_mutex_lock(&g_system_lock);
  g_system_active = false;
  pthread_mutex_unlock(&g_system_lock);
  return NULL;
}

int glass_system_start(struct glass_system_monitor **output)
{
  if (!output) return EINVAL;
  *output = NULL;
  if (glass_system_busy()) return EBUSY;
  struct glass_system_monitor *monitor = calloc(1, sizeof(*monitor));
  if (!monitor) return ENOMEM;
  int error = pthread_mutex_init(&monitor->mutex, NULL);
  if (error) { free(monitor); return error; }
  pthread_condattr_t condition;
  error = pthread_condattr_init(&condition);
  if (!error)
    {
      error = pthread_condattr_setclock(&condition, CLOCK_MONOTONIC);
      if (!error) error = pthread_cond_init(&monitor->wake, &condition);
      pthread_condattr_destroy(&condition);
    }
  if (error)
    {
      pthread_mutex_destroy(&monitor->mutex);
      free(monitor);
      return error;
    }
  pthread_attr_t attributes;
  error = pthread_attr_init(&attributes);
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
          pthread_mutex_lock(&g_system_lock);
          if (g_system_active) error = EBUSY;
          else g_system_active = true;
          pthread_mutex_unlock(&g_system_lock);
          if (!error)
            {
              monitor->references = 2;
              pthread_t thread;
              error = pthread_create(&thread, &attributes, glass_system_worker, monitor);
              if (error)
                {
                  pthread_mutex_lock(&g_system_lock);
                  g_system_active = false;
                  pthread_mutex_unlock(&g_system_lock);
                }
            }
        }
      pthread_attr_destroy(&attributes);
    }
  if (error)
    {
      pthread_cond_destroy(&monitor->wake);
      pthread_mutex_destroy(&monitor->mutex);
      free(monitor);
      return error;
    }
  *output = monitor;
  return 0;
}

int glass_system_read(struct glass_system_monitor *monitor,
                      struct glass_system_status *status)
{
  if (!monitor || !status) return EINVAL;
  pthread_mutex_lock(&monitor->mutex);
  *status = monitor->status;
  pthread_mutex_unlock(&monitor->mutex);
  return 0;
}

void glass_system_release(struct glass_system_monitor *monitor)
{
  if (!monitor) return;
  pthread_mutex_lock(&monitor->mutex);
  monitor->cancel = true;
  pthread_cond_signal(&monitor->wake);
  pthread_mutex_unlock(&monitor->mutex);
  glass_system_unref(monitor);
}
