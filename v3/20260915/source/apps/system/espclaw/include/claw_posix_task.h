/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CLAW_POSIX_TASK_H
#define CLAW_POSIX_TASK_H
#include "freertos/task.h"
/* A single owner must join each successfully created task exactly once. */
int claw_posix_task_join(TaskHandle_t task);
/* Wait for worker completion; ETIMEDOUT leaves the handle owned by caller.
 * Join remains required, including after a successful completion wait. */
int claw_posix_task_wait(TaskHandle_t task, TickType_t timeout);
int claw_posix_task_create(TaskFunction_t function, void *arg, size_t stack_size,
                           TaskHandle_t *out);
int claw_posix_task_create_priority(TaskFunction_t function, void *arg,
                                    size_t stack_size, unsigned int priority,
                                    TaskHandle_t *out);
#endif
