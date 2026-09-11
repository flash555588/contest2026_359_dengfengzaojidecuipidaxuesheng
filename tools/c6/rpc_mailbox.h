/* SPDX-License-Identifier: Apache-2.0 */
#ifndef C6_RPC_MAILBOX_H
#define C6_RPC_MAILBOX_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct c6_rpc_mailbox
{
  pthread_mutex_t lock;
  uint32_t sequence;
  uint32_t waiting;
  void *response;
};

#define C6_RPC_MAILBOX_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, 0, 0, NULL}

/* Transaction callers are serialized separately. Zero means IDs exhausted. */
static uint32_t c6_rpc_next_uid(struct c6_rpc_mailbox *box)
{
  uint32_t uid = 0;
  if (pthread_mutex_lock(&box->lock) != 0) return 0;
  if (box->sequence != UINT32_MAX) uid = ++box->sequence;
  pthread_mutex_unlock(&box->lock);
  return uid;
}

static bool c6_rpc_begin(struct c6_rpc_mailbox *box, uint32_t uid)
{
  if (!uid || pthread_mutex_lock(&box->lock) != 0) return false;
  bool available = box->waiting == 0 && box->response == NULL;
  if (available) box->waiting = uid;
  pthread_mutex_unlock(&box->lock);
  return available;
}

/* True transfers ownership to the mailbox; false leaves ownership with RX. */
static bool c6_rpc_accept(struct c6_rpc_mailbox *box, uint32_t uid, void *response)
{
  if (!response || !uid || pthread_mutex_lock(&box->lock) != 0) return false;
  bool accepted = box->waiting == uid && box->response == NULL;
  if (accepted) box->response = response;
  pthread_mutex_unlock(&box->lock);
  return accepted;
}

static bool c6_rpc_ready(struct c6_rpc_mailbox *box)
{
  if (pthread_mutex_lock(&box->lock) != 0) return false;
  bool ready = box->response != NULL;
  pthread_mutex_unlock(&box->lock);
  return ready;
}

/* Close the response window atomically and transfer ownership to caller. */
static void *c6_rpc_finish(struct c6_rpc_mailbox *box)
{
  if (pthread_mutex_lock(&box->lock) != 0) return NULL;
  void *response = box->response;
  box->response = NULL;
  box->waiting = 0;
  pthread_mutex_unlock(&box->lock);
  return response;
}

#endif
