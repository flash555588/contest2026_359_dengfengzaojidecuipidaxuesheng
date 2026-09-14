/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_LINK_STATE_H
#define C6_LINK_STATE_H

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

struct c6_link_snapshot
{
  uint64_t generation;
  bool initialized;
  bool ifup;
  bool associated;
  bool carrier_ready;
};

struct c6_link_state
{
  pthread_mutex_t lock;
  struct c6_link_snapshot value;
};

#define C6_LINK_STATE_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, {0}}

enum c6_link_change
{
  C6_LINK_STARTED,
  C6_LINK_STOPPED,
  C6_LINK_IFUP,
  C6_LINK_IFDOWN,
  C6_LINK_ASSOCIATED,
  C6_LINK_DISCONNECTED
};

static inline int c6_link_read(struct c6_link_state *state,
                               struct c6_link_snapshot *out)
{
  if (!state || !out) return -EINVAL;
  int ret = pthread_mutex_lock(&state->lock);
  if (ret) return -ret;
  *out = state->value;
  pthread_mutex_unlock(&state->lock);
  return 0;
}

/* No network or RPC calls under this lock. Preserve it across device resets. */
static inline int c6_link_update(struct c6_link_state *state,
                                 enum c6_link_change change)
{
  if (!state || change < C6_LINK_STARTED || change > C6_LINK_DISCONNECTED)
    return -EINVAL;
  int ret = pthread_mutex_lock(&state->lock);
  if (ret) return -ret;
  struct c6_link_snapshot *v = &state->value;
  if (v->generation == UINT64_MAX)
    {
      /* Fail closed; never let a wrapped ticket accept an obsolete result. */
      v->initialized = v->ifup = v->associated = v->carrier_ready = false;
      pthread_mutex_unlock(&state->lock);
      return -EOVERFLOW;
    }
  v->generation++;
  v->carrier_ready = false;
  switch (change)
    {
      case C6_LINK_STARTED:
        v->initialized = true;
        v->ifup = v->associated = false;
        break;
      case C6_LINK_STOPPED:
        v->initialized = v->ifup = v->associated = false;
        break;
      case C6_LINK_IFUP:
        v->ifup = v->initialized;
        break;
      case C6_LINK_IFDOWN:
        v->ifup = false;
        break;
      case C6_LINK_ASSOCIATED:
        v->associated = v->initialized;
        break;
      case C6_LINK_DISCONNECTED:
        v->associated = false;
        break;
    }
  pthread_mutex_unlock(&state->lock);
  return 0;
}

/* Caller publishes only after its carrier operation; stale work must retry.
 * This guards the snapshot, not netdev itself. The owner must reconcile the
 * real carrier with the newest snapshot when -EAGAIN is returned.
 */
static inline int c6_link_publish_carrier(struct c6_link_state *state,
                                          uint64_t generation, bool ready)
{
  if (!state) return -EINVAL;
  int ret = pthread_mutex_lock(&state->lock);
  if (ret) return -ret;
  struct c6_link_snapshot *v = &state->value;
  if (generation != v->generation || v->generation == UINT64_MAX)
    ret = -EAGAIN;
  else if (ready && !(v->initialized && v->ifup && v->associated))
    ret = -ENETDOWN;
  else
    {
      v->carrier_ready = ready;
      ret = 0;
    }
  pthread_mutex_unlock(&state->lock);
  return ret;
}

#endif
