/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Model registry layout includes an unused IDF partition pointer. Models are
 * verified SD buffers; no IDF partition or flash mapping API is implemented. */
typedef struct esp_partition_t esp_partition_t;
