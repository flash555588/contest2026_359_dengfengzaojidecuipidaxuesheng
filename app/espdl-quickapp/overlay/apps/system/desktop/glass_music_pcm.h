/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <mqueue.h>
#include <nuttx/audio/audio.h>
#define MUSIC_PCM_BUFFERS 4
/* The I2S bus runs two 16-bit slots per frame, so one frame is four bytes. */
#define MUSIC_PCM_FRAMES 1152
struct music_pcm {
  int fd;
  mqd_t mq;
  struct ap_buffer_s *buffers[MUSIC_PCM_BUFFERS];
  bool queued[MUSIC_PCM_BUFFERS], started, reserved, session_locked;
  unsigned rate, pending, played_samples;
  unsigned submitted_samples[MUSIC_PCM_BUFFERS];
};
int music_pcm_open(struct music_pcm *pcm, unsigned rate, unsigned volume);
int music_pcm_write(struct music_pcm *pcm, const int16_t *samples, unsigned frames, unsigned channels);
int music_pcm_volume(struct music_pcm *pcm, unsigned volume);
int music_pcm_pause(struct music_pcm *pcm, bool paused);
int music_pcm_drain(struct music_pcm *pcm);
void music_pcm_close(struct music_pcm *pcm);
int music_pcm_session_lock(void);
void music_pcm_session_unlock(void);
bool music_pcm_session_active(void);
