/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPCLAW_OPENVELA_QUEUE_H
#define ESPCLAW_OPENVELA_QUEUE_H

#include "freertos/FreeRTOS.h"

typedef struct claw_posix_queue *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
/* As in FreeRTOS, the owner must stop and join all users before deletion. */
void vQueueDelete(QueueHandle_t queue);

#endif
