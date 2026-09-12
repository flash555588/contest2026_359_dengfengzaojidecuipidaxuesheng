/* SPDX-License-Identifier: Apache-2.0 */
#include "../c6/desktop_worker.h"
#include <assert.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool entered, release_scan;
static int scan_error;

static int scan(struct c6_scan_ap *records, size_t capacity, size_t *count)
{
  assert(capacity == C6_SCAN_LIMIT);
  pthread_mutex_lock(&gate);
  entered = true;
  pthread_cond_broadcast(&wake);
  while (!release_scan) pthread_cond_wait(&wake, &gate);
  int error = scan_error;
  pthread_mutex_unlock(&gate);
  *count = 1;
  assert(c6_scan_ap_set(records, (const uint8_t *)"test", 4, -50, 6) == 0);
  return error;
}

static int connect_ap(const char *ssid, const char *password)
{
  assert(!strcmp(ssid, "test"));
  assert(!strcmp(password, "fixture-only"));
  return -ETIMEDOUT;
}

static struct c6_desktop_result wait_result(struct c6_desktop_worker *w)
{
  struct c6_desktop_result r;
  for (int i = 0; i < 1000; i++)
    {
      assert(!c6_desktop_worker_read(w, &r));
      if (!r.busy) return r;
      struct timespec delay = {0, 1000000};
      nanosleep(&delay, NULL);
    }
  assert(false);
  return r;
}

int main(void)
{
  struct c6_desktop_worker w;
  const struct c6_desktop_backend backend = {scan, connect_ap};
  assert(!c6_desktop_worker_start(&w, &backend));
  assert(c6_desktop_worker_submit(&w, C6_DESKTOP_CONNECT, "", "") == -EINVAL);
  assert(!c6_desktop_worker_submit(&w, C6_DESKTOP_SCAN, NULL, NULL));
  pthread_mutex_lock(&gate);
  while (!entered) pthread_cond_wait(&wake, &gate);
  assert(c6_desktop_worker_submit(&w, C6_DESKTOP_SCAN, NULL, NULL) == -EBUSY);
  release_scan = true;
  pthread_cond_broadcast(&wake);
  pthread_mutex_unlock(&gate);
  struct c6_desktop_result r = wait_result(&w);
  assert(!r.error && r.count == 1 && r.sequence == 1);
  assert(!memcmp(r.records[0].ssid, "test", 4));
  assert(!c6_desktop_worker_submit(&w, C6_DESKTOP_CONNECT, "test", "fixture-only"));
  r = wait_result(&w);
  assert(r.error == -ETIMEDOUT && r.count == 0 && r.sequence == 2);
  pthread_mutex_lock(&w.lock);
  for (size_t i = 0; i < sizeof(w.password); i++) assert(w.password[i] == 0);
  pthread_mutex_unlock(&w.lock);
  pthread_mutex_lock(&gate);
  scan_error = -EIO;
  pthread_mutex_unlock(&gate);
  assert(!c6_desktop_worker_submit(&w, C6_DESKTOP_SCAN, NULL, NULL));
  r = wait_result(&w);
  assert(r.error == -EIO && r.count == 0 && r.records[0].ssid_length == 0);
  assert(!c6_desktop_worker_stop(&w));
  for (size_t i = 0; i < sizeof(w.password); i++) assert(w.password[i] == 0);
  return 0;
}
