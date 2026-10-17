/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>

typedef bool (*speech_compute_cancel_fn)(void *user);
typedef int (*speech_compute_body_fn)(void *argument);
/* Only guard the pinned, allocation-free C math routines. Caller-owned arenas
 * and model blobs stay outside this scope and are released by the caller. */
int glass_speech_compute_run(speech_compute_body_fn body,void *argument,
                             speech_compute_cancel_fn cancel,void *user);
/* Inserted at output-channel boundaries of the pinned reference kernels. */
void glass_speech_compute_checkpoint(void);
/* Argument owned by the current guarded math call, or NULL outside a scope. */
void *glass_speech_compute_argument(void);
/* Two disjoint output ranges. Both tasks are joined before cancellation
 * propagates to the enclosing compute scope. Serial fallback on create error. */
void glass_speech_compute_parallel(speech_compute_body_fn body,void *left,void *right);
