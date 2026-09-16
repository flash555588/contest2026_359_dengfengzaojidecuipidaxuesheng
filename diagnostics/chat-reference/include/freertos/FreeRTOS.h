/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPCLAW_OPENVELA_FREERTOS_H
#define ESPCLAW_OPENVELA_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define pdFAIL pdFALSE
#define portMAX_DELAY UINT32_MAX
/* The port clock is milliseconds, independent of the kernel tick rate. */
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portTICK_PERIOD_MS 1

#endif
