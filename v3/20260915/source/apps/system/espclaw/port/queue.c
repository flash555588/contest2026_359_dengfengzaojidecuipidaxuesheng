/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "freertos/queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct claw_posix_queue {
    pthread_mutex_t mutex;
    pthread_cond_t readable;
    pthread_cond_t writable;
    size_t capacity;
    size_t item_size;
    size_t head;
    size_t count;
    unsigned char data[];
};

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    struct claw_posix_queue *q;
    pthread_condattr_t attr;
    if (!length || !item_size ||
        length > (SIZE_MAX - sizeof(*q)) / item_size) {
        return NULL;
    }
    q = calloc(1, sizeof(*q) + (size_t)length * item_size);
    if (!q) {
        return NULL;
    }
    if (pthread_mutex_init(&q->mutex, NULL)) {
        goto fail_alloc;
    }
    if (pthread_condattr_init(&attr)) {
        goto fail_mutex;
    }
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC)) {
        goto fail_attr;
    }
    if (pthread_cond_init(&q->readable, &attr)) {
        goto fail_attr;
    }
    if (pthread_cond_init(&q->writable, &attr)) {
        pthread_cond_destroy(&q->readable);
        goto fail_attr;
    }
    pthread_condattr_destroy(&attr);
    q->capacity = length;
    q->item_size = item_size;
    return q;

fail_attr:
    pthread_condattr_destroy(&attr);
fail_mutex:
    pthread_mutex_destroy(&q->mutex);
fail_alloc:
    free(q);
    return NULL;
}

static BaseType_t transfer(QueueHandle_t q, void *item, TickType_t wait,
                           int sending)
{
    struct timespec deadline;
    size_t slot;
    int rc = 0;
    if (!q || !item) {
        return pdFALSE;
    }
    if (wait != 0 && wait != portMAX_DELAY) {
        if (clock_gettime(CLOCK_MONOTONIC, &deadline)) {
            return pdFALSE;
        }
        deadline.tv_sec += wait / 1000;
        deadline.tv_nsec += (long)(wait % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }
    if (pthread_mutex_lock(&q->mutex)) {
        return pdFALSE;
    }
    while (sending ? q->count == q->capacity : q->count == 0) {
        pthread_cond_t *condition = sending ? &q->writable : &q->readable;
        if (!wait) {
            rc = ETIMEDOUT;
            break;
        }
        rc = wait == portMAX_DELAY ?
             pthread_cond_wait(condition, &q->mutex) :
             pthread_cond_timedwait(condition, &q->mutex, &deadline);
        if (rc) {
            break;
        }
    }
    if (!rc) {
        slot = sending ? (q->head + q->count) % q->capacity : q->head;
        if (sending) {
            memcpy(q->data + slot * q->item_size, item, q->item_size);
            q->count++;
            pthread_cond_signal(&q->readable);
        } else {
            memcpy(item, q->data + slot * q->item_size, q->item_size);
            q->head = (q->head + 1) % q->capacity;
            q->count--;
            pthread_cond_signal(&q->writable);
        }
    }
    pthread_mutex_unlock(&q->mutex);
    return rc ? pdFALSE : pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    return transfer(q, (void *)item, wait, 1);
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    return transfer(q, item, wait, 0);
}

void vQueueDelete(QueueHandle_t q)
{
    if (q) {
        pthread_cond_destroy(&q->readable);
        pthread_cond_destroy(&q->writable);
        pthread_mutex_destroy(&q->mutex);
        free(q);
    }
}
