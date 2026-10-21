/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QPK_CLIP_FRAMES 3
#define QPK_CLIP_FRAME_LIMIT (128u * 1024)
struct qpk_clip_frame { unsigned char *jpeg; size_t size; uint32_t offset_ms; };
/* A bounded temporal sample, not an AVI file or an audio recording. */
struct qpk_camera_clip {
  unsigned count;
  uint32_t duration_ms;
  struct qpk_clip_frame frames[QPK_CLIP_FRAMES];
};
/* Worker-only. USB owner is closed before success/error is returned.
 * Never touches LCD, the local inference worker or persistent storage. */
int qpk_camera_clip_capture(struct qpk_camera_clip *clip, atomic_bool *cancel);
void qpk_camera_clip_free(struct qpk_camera_clip *clip);
