/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "qpk_camera_clip.h"
#include "glass_agent_mode.h"
/* New result text only; never migrates legacy history or stores image bytes. */
int glass_camera_observation_save(uint32_t id,enum glass_agent_mode mode,
  const char *question,const char *answer,const struct qpk_camera_clip *clip,
  atomic_bool *cancel,char path[128]);
/* SD-only recording bundle: three JPEGs and a timestamp/question manifest.
 * A completed bundle survives analysis failure/cancellation. Partial bundles
 * are removed; no Flash fallback or automatic upload is allowed. */
int glass_camera_recording_check(void);
int glass_camera_recording_save(uint32_t id,const char *question,
  const struct qpk_camera_clip *clip,atomic_bool *cancel,char path[128]);
