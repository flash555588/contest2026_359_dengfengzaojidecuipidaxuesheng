/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPCLAW_OPENVELA_TASK_H
#define ESPCLAW_OPENVELA_TASK_H
#include "freertos/FreeRTOS.h"
typedef struct claw_posix_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define tskNO_AFFINITY (-1)
#define errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY pdFAIL
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
/* Only self deletion (NULL) is supported; no asynchronous cancellation. */
void vTaskDelete(TaskHandle_t task);
#endif
