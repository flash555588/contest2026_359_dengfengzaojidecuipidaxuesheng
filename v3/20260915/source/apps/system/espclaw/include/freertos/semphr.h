/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPCLAW_OPENVELA_SEMPHR_H
#define ESPCLAW_OPENVELA_SEMPHR_H
#include "freertos/FreeRTOS.h"
typedef struct claw_posix_mutex *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t mutex, TickType_t wait);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t mutex);
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex);
void vSemaphoreDelete(SemaphoreHandle_t mutex);
#endif
