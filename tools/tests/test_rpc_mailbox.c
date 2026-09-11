/* SPDX-License-Identifier: Apache-2.0 */
#include "../c6/rpc_mailbox.h"
#include <assert.h>
#include <stdio.h>

static struct c6_rpc_mailbox box = C6_RPC_MAILBOX_INITIALIZER;
static int payload;
static uint32_t current;
static bool accepted;

static void *receiver(void *arg)
{
  (void)arg;
  accepted = c6_rpc_accept(&box, current, &payload);
  return NULL;
}

int main(void)
{
  uint32_t first = c6_rpc_next_uid(&box);
  assert(first && c6_rpc_begin(&box, first));
  assert(!c6_rpc_begin(&box, first));
  assert(c6_rpc_accept(&box, first, &payload));
  assert(!c6_rpc_accept(&box, first, &payload));
  assert(c6_rpc_ready(&box));
  assert(c6_rpc_finish(&box) == &payload);
  assert(!c6_rpc_accept(&box, first, &payload));
  for (int i = 0; i < 200; i++)
    {
      current = c6_rpc_next_uid(&box);
      assert(c6_rpc_begin(&box, current));
      assert(!c6_rpc_accept(&box, first, &payload));
      pthread_t thread;
      assert(!pthread_create(&thread, NULL, receiver, NULL));
      void *response = c6_rpc_finish(&box);
      assert(!pthread_join(thread, NULL));
      assert((response == &payload) == accepted);
      assert(!c6_rpc_ready(&box));
    }
  box.sequence = UINT32_MAX;
  assert(c6_rpc_next_uid(&box) == 0);
  assert(!c6_rpc_begin(&box, 0));
  puts("PASS: duplicate/stale responses, 200 RX/finish races and UID exhaustion");
}
