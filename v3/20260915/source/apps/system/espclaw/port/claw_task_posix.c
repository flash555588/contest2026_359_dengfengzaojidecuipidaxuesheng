/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_task.h"
#include "claw_posix_task.h"

BaseType_t claw_task_create(const claw_task_config_t *config,
                            TaskFunction_t function, void *arg,
                            TaskHandle_t *out)
{
    if (out) *out = NULL;
    /* Generic POSIX stacks cannot promise physical RAM placement. */
    if (!config || !config->name || !config->name[0] ||
        config->core_id != tskNO_AFFINITY ||
        config->stack_policy != CLAW_TASK_STACK_PREFER_PSRAM) {
        return pdFAIL;
    }
    return claw_posix_task_create_priority(function, arg, config->stack_size,
                                           config->priority, out) == 0 ?
           pdPASS : pdFAIL;
}

void claw_task_delete(TaskHandle_t task)
{
    vTaskDelete(task);
}
