/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pthread.h>
typedef pthread_mutex_t _lock_t;
#define _lock_init(lock) pthread_mutex_init(lock,NULL)
#define _lock_acquire(lock) pthread_mutex_lock(lock)
#define _lock_release(lock) pthread_mutex_unlock(lock)
#define _lock_close(lock) pthread_mutex_destroy(lock)
