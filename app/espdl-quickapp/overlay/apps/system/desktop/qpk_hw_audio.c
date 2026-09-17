/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "qpk_hardware.h"
#include "qpk_storage.h"
#include "qpk_audio_store.h"
#include "glass_music_pcm.h"
#include "glass_music.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MEDIA_ROOT CONFIG_SYSTEM_DESKTOP_QPK_DIR "/.data"

/* The board has a single ES8311 codec. Wait until the music worker has closed
 * its PCM session instead of guessing how long asynchronous cancellation may
 * take; only then can another audio session reserve the shared codec safely.
 */
static int audio_take_over(void)
{
  return glass_music_stop_wait(5000);
}
#define RECORD_RATE 16000
#define RECORD_BUFFERS 4
/* P4 DMA payloads use 64-byte cache-line alignment: 4095 rounded down.
 * One descriptor per buffer also keeps the RX EOF interval constant.
 */
#define RECORD_BYTES 4032
#define RECORD_SETTLE_SAMPLES 1024
static int audio_number(const cJSON *a, const char *key, int fallback, int low, int high)
{
  cJSON *v = cJSON_GetObjectItemCaseSensitive(a, key);
  if (!v) return fallback;
  return cJSON_IsNumber(v) && v->valuedouble >= low && v->valuedouble <= high && v->valuedouble == (int)v->valuedouble ? v->valueint : -1;
}
static void le16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void le32(unsigned char *p, uint32_t v) { for (unsigned i = 0; i < 4; i++) p[i] = v >> (8 * i); }
static unsigned rd16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t rd32(const unsigned char *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static void wav_header(unsigned char h[44], unsigned bytes, unsigned rate, unsigned channels)
{
  memset(h, 0, 44);
  memcpy(h, "RIFF", 4); memcpy(h + 8, "WAVEfmt ", 8); memcpy(h + 36, "data", 4);
  le32(h + 16, 16); le16(h + 20, 1); le16(h + 34, 16);
  le32(h + 4, bytes + 36); le16(h + 22, channels); le32(h + 24, rate);
  le32(h + 28, rate * channels * 2); le16(h + 32, channels * 2); le32(h + 40, bytes);
}
static int clip_path(const char *package, const cJSON *a, char path[QPK_STORAGE_PATH_MAX], bool create)
{
  cJSON *v = cJSON_GetObjectItemCaseSensitive(a, "clip");
  const char *clip = !v ? "recording" : cJSON_IsString(v) ? v->valuestring : NULL;
  return qpk_audio_path(MEDIA_ROOT, package, clip, path, create);
}
static int write_all(int fd, const void *data, size_t size)
{
  const unsigned char *p = data;
  while (size) {
    ssize_t n = write(fd, p, size);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return n < 0 ? -errno : -EIO;
    p += n; size -= n;
  }
  return 0;
}
static int start_file(const char *path, char temporary[QPK_STORAGE_PATH_MAX + 4], int *fd)
{
  snprintf(temporary, QPK_STORAGE_PATH_MAX + 4, "%s.tmp", path);
  struct stat st;
  if (!lstat(temporary, &st) && !S_ISREG(st.st_mode)) return -EPERM;
  *fd = open(temporary, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW, 0600);
  return *fd < 0 ? -errno : 0;
}
static int finish_file(int fd, const char *temporary, const char *path, uint32_t bytes, unsigned rate, unsigned channels, int ret)
{
  unsigned char h[44]; wav_header(h, bytes, rate, channels);
  if (!ret && lseek(fd, 0, SEEK_SET) < 0) ret = -errno;
  if (!ret) ret = write_all(fd, h, sizeof(h));
  if (!ret && fsync(fd)) ret = -errno;
  if (close(fd) && !ret) ret = -errno;
  if (!ret && rename(temporary, path)) ret = -errno;
  if (ret) unlink(temporary);
  return ret;
}

static int record_clip(const char *path, const cJSON *a, cJSON *out, struct qpk_hw_progress *progress)
{
  int duration = audio_number(a, "durationMs", 10000, 0, INT32_MAX);
  if (duration < 0 || audio_number(a, "sampleRate", RECORD_RATE, RECORD_RATE, RECORD_RATE) < 0) return -EINVAL;
  uint64_t target = duration ? (uint64_t)duration * RECORD_RATE / 1000 * 2 : UINT32_MAX - 44;
  int fd = -1, file = -1, ret = 0;
  bool reserved = false, registered = false, started = false, session_locked = false;
  mqd_t mq = (mqd_t)-1;
  struct ap_buffer_s *buffers[RECORD_BUFFERS] = {0};
  unsigned char mono[RECORD_BYTES / 2], header[44];
  char temporary[QPK_STORAGE_PATH_MAX + 4] = {0}, mqname[48] = {0};
  uint32_t used = 0;
  unsigned record_buffers = 0, settle = RECORD_SETTLE_SAMPLES;
  ret = audio_take_over();
  if (ret) goto done;
  ret = music_pcm_session_lock();
  if (ret) goto done;
  session_locked = true;
  fd = open("/dev/audio/pcm_in0", O_RDWR);
  if (fd < 0) { ret = -errno; goto done; }
  if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0) {
    ret = -errno;
    goto done;
  }
  reserved = true;
  struct audio_caps_desc_s caps = {0};
  caps.caps.ac_len = sizeof(caps.caps); caps.caps.ac_type = AUDIO_TYPE_INPUT;
  caps.caps.ac_subtype = AUDIO_FMT_PCM; caps.caps.ac_channels = 2;
  caps.caps.ac_controls.hw[0] = RECORD_RATE; caps.caps.ac_controls.b[2] = 16;
  if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&caps) < 0) { ret = -errno; goto done; }
  struct mq_attr attr = {.mq_maxmsg = 10, .mq_msgsize = sizeof(struct audio_msg_s)};
  snprintf(mqname, sizeof(mqname), "/qpk-rec-%lx", (unsigned long)(uintptr_t)progress);
  mq = mq_open(mqname, O_RDWR | O_CREAT | O_EXCL, 0600, &attr);
  if (mq == (mqd_t)-1) { ret = -errno; goto done; }
  if (ioctl(fd, AUDIOIOC_REGISTERMQ, (uintptr_t)mq) < 0) { ret = -errno; goto done; }
  registered = true;
  struct ap_buffer_info_s info;
  if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info) < 0) { ret = -errno; goto done; }
  record_buffers = RECORD_BUFFERS;
  if (info.nbuffers < record_buffers) record_buffers = info.nbuffers;
  if (record_buffers < 2) { ret = -ENOBUFS; goto done; }
  for (unsigned i = 0; i < record_buffers; i++) {
    struct audio_buf_desc_s b = {.numbytes = RECORD_BYTES, .u.pbuffer = &buffers[i]};
    if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&b) != sizeof(b) || !buffers[i]) { ret = -ENOMEM; goto done; }
  }
  ret = start_file(path, temporary, &file); if (ret) goto done;
  wav_header(header, 0, RECORD_RATE, 1); ret = write_all(file, header, sizeof(header)); if (ret) goto done;
  if (ioctl(fd, AUDIOIOC_START, 0) < 0) { ret = -errno; goto done; }
  started = true;
  for (unsigned i = 0; i < record_buffers; i++) {
    buffers[i]->nbytes = buffers[i]->nmaxbytes; buffers[i]->curbyte = 0; buffers[i]->flags = 0;
    struct audio_buf_desc_s b = {.numbytes = buffers[i]->nbytes, .u.buffer = buffers[i]};
    if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&b) < 0) { ret = -errno; goto done; }
  }
  unsigned silent_waits = 0;
  while (used < target && !atomic_load(&progress->finish)) {
    if (atomic_load(&progress->cancel)) { ret = -ECANCELED; break; }
    struct timespec until; clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 100000000;
    if (until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec -= 1000000000; }
    struct audio_msg_s msg;
    ssize_t n = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL, &until);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && errno == ETIMEDOUT) { if (++silent_waits < 30) continue; ret = -ETIMEDOUT; break; }
    if (n != sizeof(msg)) { ret = n < 0 ? -errno : -EIO; break; }
    if (msg.msg_id == AUDIO_MSG_IOERR || msg.msg_id == AUDIO_MSG_COMPLETE) { ret = -EIO; break; }
    if (msg.msg_id != AUDIO_MSG_DEQUEUE) continue;
    struct ap_buffer_s *b = msg.u.ptr;
    bool known = false;
    for (unsigned i = 0; i < record_buffers; i++) if (b == buffers[i]) { known = true; break; }
    if (!known || !b->nbytes || b->nbytes > RECORD_BYTES || b->nbytes % 4) { ret = -EIO; break; }
    silent_waits = 0; unsigned count = 0, peak = 0;
    uint64_t room = target > used ? target - used : 0;
    for (unsigned i = 0; i < b->nbytes && (uint64_t)count + 2 <= room; i += 4) {
      if (settle) { settle--; continue; }
      int value = (int16_t)rd16(b->samp + i); unsigned magnitude = value < 0 ? -value : value;
      if (magnitude > peak) peak = magnitude;
      mono[count++] = b->samp[i]; mono[count++] = b->samp[i + 1];
    }
    /* Hand the buffer back to the I2S RX queue before touching storage. If the
     * driver finds the RX pending queue empty it restarts the RX DMA with
     * i2s_hal_rx_reset_fifo(), which corrupts the first sample of the next
     * transfer; with a slow file write that produced a click every 32 ms.
     */
    b->curbyte = 0; b->flags = 0; b->nbytes = b->nmaxbytes;
    struct audio_buf_desc_s next = {.numbytes = b->nbytes, .u.buffer = b};
    if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&next) < 0) { ret = -errno; break; }
    if (used > UINT32_MAX - 44 - count) { ret = -EFBIG; break; }
    ret = write_all(file, mono, count); if (ret) break;
    used += count;
    atomic_store(&progress->bytes, used); atomic_store(&progress->elapsed_ms, (uint64_t)used * 1000 / (RECORD_RATE * 2)); atomic_store(&progress->peak, peak);
    if (used >= target || atomic_load(&progress->finish) || atomic_load(&progress->cancel)) break;
  }
done:
  if (started && ioctl(fd, AUDIOIOC_STOP, 0) < 0 && !ret) ret = -errno;
  for (unsigned i = 0; i < RECORD_BUFFERS; i++) if (buffers[i]) {
    struct audio_buf_desc_s b = {.u.buffer = buffers[i]}; ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&b);
  }
  if (registered) ioctl(fd, AUDIOIOC_UNREGISTERMQ, (uintptr_t)mq);
  if (reserved) ioctl(fd, AUDIOIOC_RELEASE, 0);
  if (fd >= 0) close(fd);
  if (mq != (mqd_t)-1) { mq_close(mq); mq_unlink(mqname); }
  if (session_locked) music_pcm_session_unlock();
  if (atomic_load(&progress->cancel)) ret = -ECANCELED;
  if (!ret && !used) ret = -ENODATA;
  if (file >= 0) ret = finish_file(file, temporary, path, used, RECORD_RATE, 1, ret);
  if (!ret) { cJSON_AddNumberToObject(out, "bytes", used); cJSON_AddNumberToObject(out, "sampleRate", RECORD_RATE); cJSON_AddNumberToObject(out, "channels", 1); }
  return ret;
}

static int play_clip(const char *path, const cJSON *a, cJSON *out, struct qpk_hw_progress *progress, bool tone)
{
  int volume = audio_number(a, "volume", 30, 0, 100);
  int duration = audio_number(a, "durationMs", 300, 1, INT32_MAX), frequency = audio_number(a, "frequency", 440, 20, 20000);
  if (volume < 0 || duration < 0 || frequency < 0) return -EINVAL;
  unsigned rate = 48000, channels = 1; uint64_t remaining = (uint64_t)duration * rate / 1000 * 2;
  FILE *file = NULL;
  if (!tone) {
    file = fopen(path, "rb"); if (!file) return -errno;
    unsigned char h[44];
    if (fread(h, 1, sizeof(h), file) != sizeof(h) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVEfmt ", 8) ||
        rd32(h + 16) != 16 || rd16(h + 20) != 1 || rd16(h + 34) != 16 || memcmp(h + 36, "data", 4)) { fclose(file); return -EBADMSG; }
    channels = rd16(h + 22); rate = rd32(h + 24); remaining = rd32(h + 40);
    if ((channels != 1 && channels != 2) || rate < 8000 || rate > 48000 || remaining % (channels * 2)) { fclose(file); return -EBADMSG; }
  }
  struct music_pcm pcm = {.fd = -1, .mq = (mqd_t)-1};
  int ret = audio_take_over();
  if (!ret) ret = music_pcm_open(&pcm, rate, volume);
  uint64_t submitted = 0;
  int16_t samples[1152];
  while (!ret && remaining && !atomic_load(&progress->finish)) {
    if (atomic_load(&progress->cancel)) { ret = -ECANCELED; break; }
    uint64_t available = remaining / (channels * 2);
    unsigned frames = available > 576 ? 576 : (unsigned)available;
    unsigned bytes = frames * channels * 2;
    if (tone) for (unsigned i = 0; i < frames; i++) samples[i] = (int16_t)(sin((submitted / 2 + i) * (6.283185307179586 * frequency / rate)) * 8191);
    else if (fread(samples, 1, bytes, file) != bytes) { ret = -EIO; break; }
    ret = music_pcm_write(&pcm, samples, frames, channels);
    if (!ret) {
      remaining -= bytes; submitted += bytes;
      atomic_store(&progress->bytes, submitted > UINT32_MAX ? UINT32_MAX : submitted);
      atomic_store(&progress->elapsed_ms, (uint64_t)pcm.played_samples * 1000 / rate);
    }
  }
  if (!ret && !atomic_load(&progress->finish) && !atomic_load(&progress->cancel)) ret = music_pcm_drain(&pcm);
  cJSON_AddNumberToObject(out, "playedMs", (uint64_t)pcm.played_samples * 1000 / rate);
  music_pcm_close(&pcm); if (file) fclose(file); return ret;
}

static int save_clip(const char *path, const cJSON *a, cJSON *out, struct qpk_hw_progress *progress)
{
  int rate = audio_number(a, "sampleRate", 16000, 8000, 48000), channels = audio_number(a, "channels", 1, 1, 2);
  cJSON *data = cJSON_GetObjectItemCaseSensitive(a, "data");
  if (rate < 0 || channels < 0 || !cJSON_IsArray(data)) return -EINVAL;
  int length = cJSON_GetArraySize(data); if (!length || length % (channels * 2)) return -EINVAL;
  char temporary[QPK_STORAGE_PATH_MAX + 4]; int fd;
  int ret = start_file(path, temporary, &fd); if (ret) return ret;
  unsigned char header[44], buffer[4096]; wav_header(header, length, rate, channels);
  ret = write_all(fd, header, sizeof(header)); unsigned used = 0;
  cJSON *item;
  cJSON_ArrayForEach(item, data) {
    if (ret) break;
    if (atomic_load(&progress->cancel)) { ret = -ECANCELED; break; }
    if (!cJSON_IsNumber(item) || !(item->valuedouble >= 0 && item->valuedouble <= 255) || item->valuedouble != item->valueint) { ret = -EINVAL; break; }
    buffer[used++] = item->valueint;
    if (used == sizeof(buffer)) { ret = write_all(fd, buffer, used); used = 0; }
  }
  if (!ret && used) ret = write_all(fd, buffer, used);
  ret = finish_file(fd, temporary, path, length, rate, channels, ret);
  cJSON_AddNumberToObject(out, "bytes", length); return ret;
}

int qpk_hw_audio_execute(const char *package, const char *op, const cJSON *a, cJSON *out, struct qpk_hw_progress *progress)
{
  if (!strcmp(op, "tone")) return play_clip(NULL, a, out, progress, true);
  if (!strcmp(op, "list")) return qpk_audio_list(MEDIA_ROOT, package, out, &progress->cancel);
  bool create = !strcmp(op, "record") || !strcmp(op, "save");
  char path[QPK_STORAGE_PATH_MAX]; int ret = clip_path(package, a, path, create); if (ret) return ret;
  if (!strcmp(op, "record")) return record_clip(path, a, out, progress);
  if (!strcmp(op, "play")) return play_clip(path, a, out, progress, false);
  if (!strcmp(op, "save")) return save_clip(path, a, out, progress);
  if (!strcmp(op, "remove")) return unlink(path) ? -errno : 0;
  if (!strcmp(op, "info")) return qpk_audio_info(path, out);
  return -ENOSYS;
}
