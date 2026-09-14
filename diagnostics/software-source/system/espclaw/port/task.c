/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "claw_posix_task.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>

struct claw_posix_task {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t completed;
    int done;
    pthread_t owner;
    struct claw_posix_task *next;
    TaskFunction_t function;
    void *arg;
};

static pthread_mutex_t tasks_lock = PTHREAD_MUTEX_INITIALIZER;
static TaskHandle_t tasks;

static void task_complete(void *arg)
{
    TaskHandle_t task = arg;
    TaskHandle_t *cursor;
    pthread_mutex_lock(&tasks_lock);
    for (cursor = &tasks; *cursor; cursor = &(*cursor)->next) {
        if (*cursor == task) {
            *cursor = task->next;
            break;
        }
    }
    pthread_mutex_unlock(&tasks_lock);
    pthread_mutex_lock(&task->lock);
    task->done = 1;
    pthread_cond_broadcast(&task->completed);
    pthread_mutex_unlock(&task->lock);
}

static void *task_entry(void *arg)
{
    TaskHandle_t task = arg;
    pthread_mutex_lock(&tasks_lock);
    task->owner = pthread_self();
    task->next = tasks;
    tasks = task;
    pthread_mutex_unlock(&tasks_lock);
    task->function(task->arg);
    task_complete(task);
    return NULL;
}

int claw_posix_task_create(TaskFunction_t function, void *arg, size_t stack_size,
                           TaskHandle_t *out)
{
    return claw_posix_task_create_priority(function, arg, stack_size, 0, out);
}

int claw_posix_task_create_priority(TaskFunction_t function, void *arg,
                                    size_t stack_size, unsigned int priority,
                                    TaskHandle_t *out)
{
    pthread_attr_t attr;
    pthread_condattr_t condattr;
    TaskHandle_t task;
    int rc;
    if (out) *out = NULL;
    if (!function || !out || !stack_size) return EINVAL;
    task = calloc(1, sizeof(*task));
    if (!task) return ENOMEM;
    rc = pthread_mutex_init(&task->lock, NULL);
    if (rc) { free(task); return rc; }
    rc = pthread_condattr_init(&condattr);
    if (rc) goto fail_lock;
    rc = pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    if (!rc) rc = pthread_cond_init(&task->completed, &condattr);
    pthread_condattr_destroy(&condattr);
    if (rc) goto fail_lock;
    rc = pthread_attr_init(&attr);
    if (rc) goto fail_cond;
    rc = pthread_attr_setstacksize(&attr, stack_size);
    if (!rc && priority) {
#ifdef __NuttX__
        struct sched_param param = {0};
        int minimum = sched_get_priority_min(SCHED_RR);
        int maximum = sched_get_priority_max(SCHED_RR);
        if (minimum < 0 || maximum < 0 || priority < (unsigned int)minimum ||
            priority > (unsigned int)maximum) {
            rc = EINVAL;
        } else {
            param.sched_priority = (int)priority;
            rc = pthread_attr_setschedpolicy(&attr, SCHED_RR);
            if (!rc) rc = pthread_attr_setschedparam(&attr, &param);
            if (!rc) rc = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        }
#else
        rc = ENOTSUP;
#endif
    }
    if (!rc) {
        task->function = function;
        task->arg = arg;
        rc = pthread_create(&task->thread, &attr, task_entry, task);
    }
    pthread_attr_destroy(&attr);
    if (rc) goto fail_cond;
    *out = task;
    return 0;

fail_cond:
    pthread_cond_destroy(&task->completed);
fail_lock:
    pthread_mutex_destroy(&task->lock);
    free(task);
    return rc;
}

int claw_posix_task_wait(TaskHandle_t task, TickType_t timeout)
{
    struct timespec deadline;
    int rc;
    if (!task) return EINVAL;
    if (pthread_equal(task->thread, pthread_self())) return EDEADLK;
    if (timeout && timeout != portMAX_DELAY) {
        if (clock_gettime(CLOCK_MONOTONIC, &deadline)) return errno;
        deadline.tv_sec += timeout / 1000;
        deadline.tv_nsec += (long)(timeout % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }
    rc = pthread_mutex_lock(&task->lock);
    if (rc) return rc;
    while (!task->done) {
        if (!timeout) { rc = ETIMEDOUT; break; }
        rc = timeout == portMAX_DELAY ?
             pthread_cond_wait(&task->completed, &task->lock) :
             pthread_cond_timedwait(&task->completed, &task->lock, &deadline);
        if (rc) break;
    }
    if (task->done) rc = 0;
    pthread_mutex_unlock(&task->lock);
    return rc;
}

int claw_posix_task_join(TaskHandle_t task)
{
    int rc;
    if (!task) return EINVAL;
    rc = pthread_join(task->thread, NULL);
    if (!rc) {
        pthread_cond_destroy(&task->completed);
        pthread_mutex_destroy(&task->lock);
        free(task);
    }
    return rc;
}

TickType_t xTaskGetTickCount(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) abort();
    return (TickType_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

void vTaskDelay(TickType_t ticks)
{
    struct timespec delay = {ticks / 1000, (long)(ticks % 1000) * 1000000L};
    while (nanosleep(&delay, &delay) && errno == EINTR) {}
}

void vTaskDelete(TaskHandle_t task)
{
    /* Fail loudly instead of pretending to cancel a running worker safely. */
    if (task) abort();
    pthread_mutex_lock(&tasks_lock);
    for (task = tasks; task; task = task->next) {
        if (pthread_equal(task->owner, pthread_self())) break;
    }
    pthread_mutex_unlock(&tasks_lock);
    if (!task) abort();
    task_complete(task);
    pthread_exit(NULL);
}
