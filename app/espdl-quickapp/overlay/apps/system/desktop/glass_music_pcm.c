/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_music_pcm.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
extern void board_music_amplifier(bool enabled);

/* The board has one ES8311 codec and one shared I2S DMA engine. Keep a
 * session exclusively owned until STOP, buffer release, and MQ teardown have
 * all completed; RESERVE alone cannot serialize the surrounding teardown. */
static pthread_mutex_t g_pcm_session_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool g_pcm_session_active;

int music_pcm_session_lock(void)
{
  int ret;
  do
    {
      ret = pthread_mutex_lock(&g_pcm_session_lock);
    }
  while (ret == EINTR);

  if (ret == 0)
    {
      atomic_store(&g_pcm_session_active, true);
    }

  return ret ? -ret : 0;
}

void music_pcm_session_unlock(void)
{
  atomic_store(&g_pcm_session_active, false);
  pthread_mutex_unlock(&g_pcm_session_lock);
}

bool music_pcm_session_active(void)
{
  return atomic_load(&g_pcm_session_active);
}

static int pcm_message(struct music_pcm *pcm)
{
  struct timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += 2;
  struct audio_msg_s msg;
  ssize_t n;
  do { n = mq_timedreceive(pcm->mq, (char *)&msg, sizeof(msg), NULL, &deadline); }
  while (n < 0 && errno == EINTR);
  if (n < 0) return -errno;
  if (msg.msg_id == AUDIO_MSG_DEQUEUE) {
    for (unsigned i = 0; i < MUSIC_PCM_BUFFERS; i++)
      if (pcm->buffers[i] == msg.u.ptr && pcm->queued[i]) {
        pcm->queued[i] = false;
        pcm->pending--;
        pcm->played_samples += pcm->submitted_samples[i];
        return 0;
      }
    return -EIO;
  }
  if (msg.msg_id == AUDIO_MSG_IOERR || msg.msg_id == AUDIO_MSG_COMPLETE) {
    return -EIO;
  }
  return 0;
}

int music_pcm_volume(struct music_pcm *pcm, unsigned volume)
{
  if (pcm->fd < 0) return -ENODEV;
  struct audio_caps_desc_s caps = {0};
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = AUDIO_TYPE_FEATURE;
  caps.caps.ac_format.hw = AUDIO_FU_VOLUME;
  caps.caps.ac_controls.hw[0] = (volume > 100 ? 100 : volume) * 10;
  return ioctl(pcm->fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0 ? -errno : 0;
}

int music_pcm_open(struct music_pcm *pcm, unsigned rate, unsigned volume)
{
  memset(pcm, 0, sizeof(*pcm)); pcm->fd = -1; pcm->mq = (mqd_t)-1;
  if (rate < 8000 || rate > 48000) return -EINVAL;
  int ret = music_pcm_session_lock();
  if (ret) return ret;
  pcm->session_locked = true;
  pcm->fd = open("/dev/audio/pcm0", O_RDWR);
  /* The board exposes its single ADC/DAC lower half under pcm_in0.
   * Reuse that upper half so recording and playback share RESERVE ownership. */
  if (pcm->fd < 0 && errno == ENOENT) pcm->fd = open("/dev/audio/pcm_in0", O_RDWR);
  if (pcm->fd < 0) { ret = -errno; goto fail; }
  ret = -EIO;
  if (ioctl(pcm->fd, AUDIOIOC_RESERVE, 0) < 0) { ret = -errno; goto fail; }
  pcm->reserved = true;
  struct audio_caps_desc_s caps = {0};
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = AUDIO_TYPE_OUTPUT;
  caps.caps.ac_channels = 1;
  caps.caps.ac_controls.hw[0] = rate;
  caps.caps.ac_controls.b[2] = 16;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  if (ioctl(pcm->fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0) { ret = -errno; goto fail; }
  struct mq_attr attr = {.mq_maxmsg = 12, .mq_msgsize = sizeof(struct audio_msg_s)};
  mq_unlink("/musicpcm");
  pcm->mq = mq_open("/musicpcm", O_CREAT | O_RDWR, 0600, &attr);
  if (pcm->mq == (mqd_t)-1) { ret = -errno; goto fail; }
  if (ioctl(pcm->fd, AUDIOIOC_REGISTERMQ, (unsigned long)pcm->mq) < 0) { ret = -errno; goto fail; }
  /* The upper half learns its allocation limit through GETBUFFERINFO. */
  struct ap_buffer_info_s info = {0};
  if (ioctl(pcm->fd, AUDIOIOC_GETBUFFERINFO, (unsigned long)&info) < 0) { ret = -errno; goto fail; }
  if (info.nbuffers < MUSIC_PCM_BUFFERS) { ret = -ENOBUFS; goto fail; }
  for (unsigned i = 0; i < MUSIC_PCM_BUFFERS; i++) {
    struct audio_buf_desc_s desc = {.numbytes = MUSIC_PCM_FRAMES * 4, .u.pbuffer = &pcm->buffers[i]};
    int allocated = ioctl(pcm->fd, AUDIOIOC_ALLOCBUFFER, (unsigned long)&desc);
    if (allocated != sizeof(desc) || !pcm->buffers[i]) { ret = allocated < 0 ? -errno : -ENOMEM; goto fail; }
  }
  pcm->rate = rate;
  ret = music_pcm_volume(pcm, volume);
  if (ret) goto fail;
  printf("[music] PCM opened rate=%u mono volume=%u\n", rate, volume);
  return 0;
fail:
  music_pcm_close(pcm);
  return ret;
}

int music_pcm_write(struct music_pcm *pcm, const int16_t *samples, unsigned frames, unsigned channels)
{
  if (!frames || (channels != 1 && channels != 2)) return -EINVAL;
  while (frames) {
    unsigned index, chunk = frames > MUSIC_PCM_FRAMES ? MUSIC_PCM_FRAMES : frames;
    for (;;) {
      for (index = 0; index < MUSIC_PCM_BUFFERS; index++) if (!pcm->queued[index]) break;
      if (index < MUSIC_PCM_BUFFERS) break;
      int ret = pcm_message(pcm);
      if (ret) return ret;
    }
    struct ap_buffer_s *buffer = pcm->buffers[index];
    int16_t *out = (int16_t *)buffer->samp;
    /* The I2S peripheral transfers two 16-bit slots per frame. Writing one
     * mono sample per frame let the DAC consume two samples per clock, which
     * played everything at double speed with aliasing. Duplicate the sample
     * into both slots so frame timing, pitch and duration stay correct. */
    for (unsigned i = 0; i < chunk; i++) {
      int16_t value = channels == 1 ? samples[i] :
        (int16_t)(((int32_t)samples[i*2] + samples[i*2+1]) / 2);
      out[i*2] = value; out[i*2+1] = value;
    }
    buffer->nbytes = chunk * 4; buffer->curbyte = 0; buffer->flags = 0;
    struct audio_buf_desc_s desc = {.numbytes = buffer->nbytes, .u.buffer = buffer};
    pcm->submitted_samples[index] = chunk;
    if (ioctl(pcm->fd, AUDIOIOC_ENQUEUEBUFFER, (unsigned long)&desc) < 0) return -errno;
    pcm->queued[index] = true; pcm->pending++;
    if (!pcm->started && pcm->pending >= 2) {
      if (ioctl(pcm->fd, AUDIOIOC_START, 0) < 0) return -errno;
      pcm->started = true;
      board_music_amplifier(true);
    }
    samples += chunk * channels; frames -= chunk;
  }
  return 0;
}

int music_pcm_pause(struct music_pcm *pcm, bool paused)
{
  if (!pcm->started) return 0;
  return ioctl(pcm->fd, paused ? AUDIOIOC_PAUSE : AUDIOIOC_RESUME, 0) < 0 ? -errno : 0;
}

int music_pcm_drain(struct music_pcm *pcm)
{
  if (pcm->pending && !pcm->started) {
    if (ioctl(pcm->fd, AUDIOIOC_START, 0) < 0) return -errno;
    pcm->started = true;
    board_music_amplifier(true);
  }
  while (pcm->pending) { int ret = pcm_message(pcm); if (ret) return ret; }
  return 0;
}

void music_pcm_close(struct music_pcm *pcm)
{
  bool session_locked = pcm->session_locked;
  if (pcm->fd >= 0) {
    if (pcm->reserved) board_music_amplifier(false);
    /* A failure or cancellation may leave one primed buffer below the normal
     * two-buffer start threshold. Start it with the amplifier muted so STOP
     * can synchronously return that buffer through the lower-half driver.
     */
    if (pcm->reserved && pcm->pending && !pcm->started &&
        ioctl(pcm->fd, AUDIOIOC_START, 0) == 0) pcm->started = true;
    /* STOP joins the lower-half worker before buffers can be reclaimed. */
    if (pcm->started) {
      ioctl(pcm->fd, AUDIOIOC_STOP, 0);
    }
    for (unsigned i = 0; i < MUSIC_PCM_BUFFERS; i++) if (pcm->buffers[i]) {
      struct audio_buf_desc_s desc = {.u.buffer = pcm->buffers[i]};
      ioctl(pcm->fd, AUDIOIOC_FREEBUFFER, (unsigned long)&desc);
    }
    if (pcm->mq != (mqd_t)-1) ioctl(pcm->fd, AUDIOIOC_UNREGISTERMQ, (unsigned long)pcm->mq);
    if (pcm->reserved) ioctl(pcm->fd, AUDIOIOC_RELEASE, 0);
    close(pcm->fd);
  }
  if (pcm->mq != (mqd_t)-1) { mq_close(pcm->mq); mq_unlink("/musicpcm"); }
  memset(pcm, 0, sizeof(*pcm)); pcm->fd = -1; pcm->mq = (mqd_t)-1;
  if (session_locked) music_pcm_session_unlock();
}
