/* SPDX-License-Identifier: Apache-2.0 */
#include "../c6/link_state.h"
#include <assert.h>

static struct c6_link_state state = C6_LINK_STATE_INITIALIZER;

static void *events(void *unused)
{
  (void)unused;
  for (int i = 0; i < 10000; i++)
    {
      assert(c6_link_update(&state, C6_LINK_ASSOCIATED) == 0);
      assert(c6_link_update(&state, C6_LINK_DISCONNECTED) == 0);
    }
  return NULL;
}

int main(void)
{
  struct c6_link_snapshot snapshot;
  assert(c6_link_read(&state, NULL) == -EINVAL);
  assert(c6_link_update(&state, C6_LINK_ASSOCIATED) == 0);
  assert(c6_link_read(&state, &snapshot) == 0 && !snapshot.associated);
  assert(c6_link_update(&state, C6_LINK_STARTED) == 0);
  assert(c6_link_update(&state, C6_LINK_IFUP) == 0);
  assert(c6_link_update(&state, C6_LINK_ASSOCIATED) == 0);
  assert(c6_link_read(&state, &snapshot) == 0);
  assert(!snapshot.carrier_ready);
  uint64_t old = snapshot.generation;
  assert(c6_link_update(&state, C6_LINK_DISCONNECTED) == 0);
  assert(c6_link_publish_carrier(&state, old, true) == -EAGAIN);
  assert(c6_link_read(&state, &snapshot) == 0);
  assert(!snapshot.associated && !snapshot.carrier_ready);
  assert(c6_link_publish_carrier(&state, snapshot.generation, true) == -ENETDOWN);
  assert(c6_link_update(&state, C6_LINK_ASSOCIATED) == 0);
  assert(c6_link_read(&state, &snapshot) == 0);
  assert(c6_link_publish_carrier(&state, snapshot.generation, true) == 0);
  assert(c6_link_update(&state, C6_LINK_IFDOWN) == 0);
  assert(c6_link_read(&state, &snapshot) == 0);
  assert(!snapshot.ifup && !snapshot.carrier_ready);
  assert(c6_link_update(&state, C6_LINK_IFUP) == 0);
  pthread_t thread;
  assert(pthread_create(&thread, NULL, events, NULL) == 0);
  for (int i = 0; i < 10000; i++)
    {
      assert(c6_link_read(&state, &snapshot) == 0);
      int ret = c6_link_publish_carrier(&state, snapshot.generation, true);
      assert(ret == 0 || ret == -EAGAIN || ret == -ENETDOWN);
      assert(c6_link_read(&state, &snapshot) == 0);
      assert(!snapshot.carrier_ready ||
             (snapshot.initialized && snapshot.ifup && snapshot.associated));
    }
  assert(pthread_join(thread, NULL) == 0);
  assert(c6_link_update(&state, C6_LINK_STOPPED) == 0);
  assert(c6_link_read(&state, &snapshot) == 0);
  assert(!snapshot.initialized && !snapshot.carrier_ready);
  state.value.generation = UINT64_MAX;
  assert(c6_link_update(&state, C6_LINK_STARTED) == -EOVERFLOW);
  assert(c6_link_publish_carrier(&state, UINT64_MAX, true) == -EAGAIN);
  return 0;
}
