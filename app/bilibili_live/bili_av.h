/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_AV_H
#define BILI_AV_H
#include "bili_decoder.h"
#include <stdatomic.h>
#include <stdbool.h>
struct bili_av;
struct bili_av_ops {
  bili_timed_frame_fn frame;
  int (*audio_open)(void *, unsigned);
  int (*audio_write)(void *, const int16_t *, unsigned, unsigned);
  unsigned (*audio_time)(void *);
  void (*audio_close)(void *);
};
struct bili_av_stats {
  unsigned decoded, displayed, decode_skip, render_skip, resync, lag;
  unsigned audio_frames, audio_skip;
  unsigned audio_depth,audio_peak,audio_bytes_peak;
  unsigned audio_queue_ms,audio_live_ms,video_live_ms; /* Relative to received media, not broadcaster wall time. */
  uint64_t decode_us;
  uint64_t decoder_us; /* Excludes timed-frame callback, conversion and pacing. */
};
struct bili_av *bili_av_create(const struct bili_av_ops *, void *, atomic_bool *);
int bili_av_video(struct bili_av *, const uint8_t *, size_t, uint32_t);
int bili_av_audio(struct bili_av *, const uint8_t *, size_t, uint32_t);
int bili_av_error(struct bili_av *);
void bili_av_stats(struct bili_av *, struct bili_av_stats *);
void bili_av_destroy(struct bili_av *);
#endif
