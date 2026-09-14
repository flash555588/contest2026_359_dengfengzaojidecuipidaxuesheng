/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_voice.h"
#include <nuttx/audio/audio.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* P4's existing I2S0 uses two 16-bit slots. ES8311 transmits in the left
 * slot; extract it explicitly instead of labelling stereo bytes as mono. */
#define BUFFERS 2
#define BUFFER_BYTES 4096

static int64_t now_ms(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int claw_voice_capture(unsigned int seconds, atomic_bool *cancel,
                       atomic_bool *finish, unsigned char **wav, size_t *size)
{
  int fd = -1, ret = -EIO;
  bool reserved = false, registered = false, started = false;
  mqd_t mq = (mqd_t)-1;
  struct ap_buffer_s *buffers[BUFFERS] = {0};
  unsigned char *data = NULL;
  size_t used = 0, capacity;
  char mqname[48] = {0};
  if (!wav || !size || !cancel || !finish || !seconds || seconds > CLAW_VOICE_MAX_SECONDS)
    return -EINVAL;
  *wav = NULL; *size = 0;
  capacity = seconds * CLAW_VOICE_RATE * 2;
  data = malloc(capacity + 44);
  if (!data) return -ENOMEM;
  fd = open("/dev/audio/pcm_in0", O_RDWR);
  if (fd < 0) { ret = -errno; goto done; }
  if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0) { ret = -errno; goto done; }
  reserved = true;
  struct audio_caps_desc_s caps = {0};
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = AUDIO_TYPE_INPUT;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  caps.caps.ac_channels = 2;
  caps.caps.ac_controls.hw[0] = CLAW_VOICE_RATE;
  caps.caps.ac_controls.b[2] = 16;
  if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&caps) < 0) { ret = -errno; goto done; }
  struct mq_attr attr = {.mq_maxmsg = BUFFERS + 8, .mq_msgsize = sizeof(struct audio_msg_s)};
  snprintf(mqname, sizeof(mqname), "/claw-voice-%ld", (long)getpid());
  mq = mq_open(mqname, O_RDWR | O_CREAT | O_EXCL, 0600, &attr);
  if (mq == (mqd_t)-1) { ret = -errno; goto done; }
  if (ioctl(fd, AUDIOIOC_REGISTERMQ, (uintptr_t)mq) < 0) { ret = -errno; goto done; }
  registered = true;
  for (int i = 0; i < BUFFERS; i++) {
    struct audio_buf_desc_s b = {.numbytes = BUFFER_BYTES, .u.pbuffer = &buffers[i]};
    if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&b) != sizeof(b) || !buffers[i]) {
      ret = -ENOMEM; goto done;
    }
  }
  /* Start before enqueue, so an unsuccessful start owns no queued buffer. */
  if (ioctl(fd, AUDIOIOC_START, 0) < 0) { ret = -errno; goto done; }
  started = true;
  for (int i = 0; i < BUFFERS; i++) {
    buffers[i]->nbytes = buffers[i]->nmaxbytes;
    buffers[i]->curbyte = 0; buffers[i]->flags = 0;
    struct audio_buf_desc_s b = {.numbytes = buffers[i]->nbytes, .u.buffer = buffers[i]};
    if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&b) < 0) { ret = -errno; goto done; }
  }
  int64_t deadline = now_ms() + seconds * 1000 + 3000;
  int64_t last_data = now_ms();
  ret = 0;
  while (used < capacity && !atomic_load(finish)) {
    if (atomic_load(cancel)) { ret = -ECANCELED; break; }
    if (now_ms() >= deadline || now_ms() - last_data > 2000) { ret = -ETIMEDOUT; break; }
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 100000000;
    if (until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec -= 1000000000; }
    struct audio_msg_s msg;
    ssize_t n = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL, &until);
    if (n < 0 && (errno == EINTR || errno == ETIMEDOUT)) continue;
    if (n != sizeof(msg)) { ret = n < 0 ? -errno : -EIO; break; }
    if (msg.msg_id == AUDIO_MSG_COMPLETE) { ret = -EPIPE; break; }
    if (msg.msg_id != AUDIO_MSG_DEQUEUE) continue;
    struct ap_buffer_s *b = msg.u.ptr;
    if (b != buffers[0] && b != buffers[1]) { ret = -EIO; break; }
    if (!b->nbytes || b->nbytes > b->nmaxbytes || (b->nbytes % 4)) { ret = -EIO; break; }
    last_data = now_ms();
    unsigned int peak = 0;
    for (size_t i = 0; i + 3 < b->nbytes && used < capacity; i += 4) {
      int value = (int16_t)((unsigned int)b->samp[i] | (unsigned int)b->samp[i+1] << 8);
      unsigned int magnitude = value < 0 ? -value : value;
      if (magnitude > peak) peak = magnitude;
      data[44 + used++] = b->samp[i]; data[44 + used++] = b->samp[i+1];
    }
    claw_voice_progress(used * 1000 / (CLAW_VOICE_RATE * 2), peak);
    if (used >= capacity || atomic_load(finish) || atomic_load(cancel)) break;
    b->curbyte = 0; b->flags = 0; b->nbytes = b->nmaxbytes;
    struct audio_buf_desc_s desc = {.numbytes = b->nbytes, .u.buffer = b};
    if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&desc) < 0) { ret = -errno; break; }
  }
done:
  /* STOP joins the codec worker, which drains DMA before buffers are freed.
   * If the driver blocks, this service worker retains all owned resources;
   * the desktop remains responsive and cannot launch a second capture. */
  if (started && ioctl(fd, AUDIOIOC_STOP, 0) < 0 && !ret) ret = -errno;
  for (int i = 0; i < BUFFERS; i++) if (buffers[i]) {
    struct audio_buf_desc_s b = {.u.buffer = buffers[i]};
    ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&b);
  }
  if (registered) ioctl(fd, AUDIOIOC_UNREGISTERMQ, (uintptr_t)mq);
  if (reserved) ioctl(fd, AUDIOIOC_RELEASE, 0);
  if (fd >= 0) close(fd);
  if (mq != (mqd_t)-1) { mq_close(mq); mq_unlink(mqname); }
  if (atomic_load(cancel)) ret = -ECANCELED;
  if (!ret && used < CLAW_VOICE_RATE / 2) ret = -ENODATA; /* at least 250 ms */
  if (!ret) { claw_voice_wav_header(data, used); *wav = data; *size = used + 44; }
  else free(data);
  return ret;
}
