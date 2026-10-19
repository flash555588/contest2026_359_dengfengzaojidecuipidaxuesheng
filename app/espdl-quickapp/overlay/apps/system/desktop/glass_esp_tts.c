/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_esp_tts.h"
#include "esp_tts.h"
#include "esp_tts_voice_template.h"
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static atomic_uint tts_speed = 3;
int glass_esp_tts_set_speed(unsigned speed)
{
  if (speed > 5) return -EINVAL;
  atomic_store(&tts_speed, speed);
  return 0;
}
unsigned glass_esp_tts_speed(void)
{
  return atomic_load(&tts_speed);
}
/* Private redirect for this pinned TTS archive only. Per-character INFO logs
 * flood the USB console; retain warnings and errors without changing the
 * board's global logging or the vendor synthesis instructions. */
void speech_tts_log_write(int level, const char *tag, const char *format, ...)
{
  (void)tag;
  if (level > 2) return;
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}
static uint64_t tts_now(void)
{
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

int glass_esp_synthesize(const char *text, speech_pcm_fn write_pcm,
                         speech_cancel_fn cancel, void *user)
{
  if (!text || !text[0] || strlen(text) > 1024 || !write_pcm) return -EINVAL;
  if (cancel && cancel(user)) return -ECANCELED;
  unsigned speed = glass_esp_tts_speed();
  esp_tts_voice_t template = esp_tts_voice_template;
  if (template.syll_num < 1 || template.syll_num > 4096 || !template.sylls) return -EBADMSG;
  const unsigned char *voice_data;void *voice_owned;
  int loaded=glass_esp_tts_voice_load(&voice_data,&voice_owned,cancel,user);
  if(loaded)return loaded;
  char **sylls = malloc((size_t)template.syll_num * sizeof(*sylls));
  if (!sylls) {glass_esp_tts_voice_release(voice_owned);return -ENOMEM;}
  memcpy(sylls, template.sylls, (size_t)template.syll_num * sizeof(*sylls));
  template.sylls = sylls;
  esp_tts_voice_t *voice = esp_tts_voice_set_init(&template, (void *)voice_data);
  if (!voice) { free(sylls);glass_esp_tts_voice_release(voice_owned); return -ENOMEM; }
  if (voice->sample_rate != GLASS_ESP_TTS_RATE || voice->bit_width != 16) {
    esp_tts_voice_set_free(voice); free(sylls);glass_esp_tts_voice_release(voice_owned); return -EBADMSG;
  }
  esp_tts_handle_t tts = esp_tts_create(voice);
  if (!tts) { esp_tts_voice_set_free(voice); free(sylls);glass_esp_tts_voice_release(voice_owned); return -ENOMEM; }
  uint64_t started = tts_now(), compute = 0;
  size_t samples = 0;
  int ret = 0;
  if (cancel && cancel(user)) { ret = -ECANCELED; goto done; }
  uint64_t at = tts_now();
  int parsed = esp_tts_parse_chinese(tts, text);
  compute += tts_now() - at;
  if (!parsed) { ret = -EBADMSG; goto done; }
  for (unsigned calls = 0; calls < 10000; calls++) {
    if (cancel && cancel(user)) { ret = -ECANCELED; break; }
    int length = 0;
    at = tts_now();
    short *pcm = esp_tts_stream_play(tts, &length, speed);
    compute += tts_now() - at;
    if (length < 0 || (length && !pcm)) { ret = -EBADMSG; break; }
    if (!length) { if (!samples) ret = -ENODATA; break; }
    /* Bound malformed streams without buffering the whole reply. */
    if ((size_t)length > 16000u * 180u - samples) { ret = -EFBIG; break; }
    ret = write_pcm(user, pcm, (size_t)length);
    if (ret) break;
    samples += (size_t)length;
    if (calls == 9999) ret = -ELOOP;
  }
done:
  esp_tts_destroy(tts);
  esp_tts_voice_set_free(voice);
  free(sylls);
  glass_esp_tts_voice_release(voice_owned);
  printf("[speech] backend=esp-tts rate=16000 samples=%u compute_ms=%lu elapsed_ms=%lu result=%d speed=%u\n",
    (unsigned)samples, (unsigned long)compute, (unsigned long)(tts_now() - started), ret, speed);
  return ret;
}
