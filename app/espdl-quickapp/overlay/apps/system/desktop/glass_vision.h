/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdatomic.h>
#include "qpk_camera_clip.h"
/* Called only with an owned, completed camera receipt, never a model path. */
char *glass_vision_analyze(const char *path,const char *question,atomic_bool *cancel);
/* Owned temporal sample, uploaded once after the camera has been closed. */
char *glass_vision_analyze_clip(const struct qpk_camera_clip *clip,const char *question,atomic_bool *cancel);
/* Consumes owned JPEG buffers on every exit, retaining only timestamps/sizes.
 * JPEG and Base64 staging buffers are released before the HTTPS exchange. */
char *glass_vision_analyze_clip_take(struct qpk_camera_clip *clip,const char *question,atomic_bool *cancel);
