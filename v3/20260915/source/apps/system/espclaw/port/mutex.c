/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "freertos/semphr.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct claw_posix_mutex { pthread_mutex_t native; int recursive; };

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t mutex = calloc(1, sizeof(*mutex));
    if (mutex && pthread_mutex_init(&mutex->native, NULL)) {
        free(mutex);
        mutex = NULL;
    }
    return mutex;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
#if defined(__NuttX__) && !defined(CONFIG_PTHREAD_MUTEX_TYPES)
    return NULL;
#else
    pthread_mutexattr_t attr;
    SemaphoreHandle_t mutex = calloc(1, sizeof(*mutex));
    if (!mutex) return NULL;
    int rc = pthread_mutexattr_init(&attr);
    if (rc) { free(mutex); return NULL; }
    rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (!rc) rc = pthread_mutex_init(&mutex->native, &attr);
    pthread_mutexattr_destroy(&attr);
    if (rc) { free(mutex); return NULL; }
    mutex->recursive = 1;
    return mutex;
#endif
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t mutex, TickType_t wait)
{
    return mutex && mutex->recursive ? xSemaphoreTake(mutex, wait) : pdFALSE;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t mutex)
{
    return mutex && mutex->recursive ? xSemaphoreGive(mutex) : pdFALSE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{
    struct timespec start, now;
    const struct timespec pause = {0, 1000000L};
    int rc;
    if (!mutex) {
        return pdFALSE;
    }
    if (wait == portMAX_DELAY) {
        return pthread_mutex_lock(&mutex->native) == 0 ? pdTRUE : pdFALSE;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &start)) {
        return pdFALSE;
    }
    for (;;) {
        rc = pthread_mutex_trylock(&mutex->native);
        if (rc == 0) {
            return pdTRUE;
        }
        if (rc != EBUSY || !wait || clock_gettime(CLOCK_MONOTONIC, &now)) {
            return pdFALSE;
        }
        if ((int64_t)(now.tv_sec - start.tv_sec) * 1000000000LL +
            now.tv_nsec - start.tv_nsec >= (int64_t)wait * 1000000LL) {
            return pdFALSE;
        }
        /* Portable monotonic timeout; NuttX timedlock uses wall time. */
        nanosleep(&pause, NULL);
    }
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    return mutex && pthread_mutex_unlock(&mutex->native) == 0 ? pdTRUE : pdFALSE;
}

void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    if (mutex && pthread_mutex_destroy(&mutex->native) == 0) {
        free(mutex);
    }
}
