/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_voice.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#define BOUNDARY "espclaw-voice-89bcd571a2e64aa79de2442537a156cd"
#define MAX_WAV (44 + CLAW_VOICE_MAX_SECONDS * CLAW_VOICE_RATE * 2)

static void le32(unsigned char *p, uint32_t v)
{ for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }

void claw_voice_wav_header(unsigned char out[44], size_t pcm_size)
{
  memset(out, 0, 44);
  memcpy(out, "RIFF", 4); le32(out + 4, pcm_size + 36);
  memcpy(out + 8, "WAVEfmt ", 8); le32(out + 16, 16);
  out[20] = 1; out[22] = 1; /* PCM, mono, signed 16-bit little endian */
  le32(out + 24, CLAW_VOICE_RATE); le32(out + 28, CLAW_VOICE_RATE * 2);
  out[32] = 2; out[34] = 16;
  memcpy(out + 36, "data", 4); le32(out + 40, pcm_size);
}

bool claw_voice_valid_text(const char *s, size_t limit)
{
  if (!s || !*s || strnlen(s, limit + 1) > limit) return false;
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    unsigned int c = *p++;
    if (c < 0x80) { if ((c < 32 && c != '\n' && c != '\t') || c == 127) return false; continue; }
    int n; unsigned int min;
    if (c >= 0xc2 && c <= 0xdf) { n = 1; min = 0x80; c &= 0x1f; }
    else if (c >= 0xe0 && c <= 0xef) { n = 2; min = 0x800; c &= 0x0f; }
    else if (c >= 0xf0 && c <= 0xf4) { n = 3; min = 0x10000; c &= 7; }
    else return false;
    while (n--) { if ((*p & 0xc0) != 0x80) return false; c = (c << 6) | (*p++ & 0x3f); }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
  }
  return true;
}

bool claw_voice_valid_url(const char *s)
{
  if (!s || strnlen(s, 1025) > 1024 || strncmp(s, "https://", 8)) return false;
  const char *host = s + 8;
  if (!*host || *host == '/' || *host == ':') return false;
  for (const unsigned char *p = (const unsigned char *)host; *p; p++)
    if (*p <= 32 || *p >= 127 || strchr("@?#\\", *p)) return false;
  return true;
}

const char *claw_voice_content_type(void)
{ return "multipart/form-data; boundary=" BOUNDARY; }

int claw_voice_multipart(const char *model, const unsigned char *wav, size_t size,
                          unsigned char **body, size_t *length)
{
  if (!body || !length) return -EINVAL;
  *body = NULL; *length = 0;
  if (!model || !*model || strnlen(model, 129) > 128 || !wav || size <= 44 || size > MAX_WAV)
    return -EINVAL;
  for (const unsigned char *p = (const unsigned char *)model; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || strchr("-_./:", *p))) return -EINVAL;
  if (memcmp(wav, "RIFF", 4) || memcmp(wav + 8, "WAVE", 4)) return -EINVAL;
  for (size_t i = 0; i + strlen(BOUNDARY) <= size; i++)
    if (!memcmp(wav + i, BOUNDARY, strlen(BOUNDARY))) return -EINVAL;
  char prefix[640];
  int n = snprintf(prefix, sizeof(prefix),
    "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
    "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
    "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n", model);
  const char suffix[] = "\r\n--" BOUNDARY "--\r\n";
  if (n <= 0 || n >= sizeof(prefix)) return -EOVERFLOW;
  *length = n + size + sizeof(suffix) - 1;
  *body = malloc(*length);
  if (!*body) return -ENOMEM;
  memcpy(*body, prefix, n);
  memcpy(*body + n, wav, size);
  memcpy(*body + n + size, suffix, sizeof(suffix) - 1);
  return 0;
}

int claw_voice_parse_transcript(const char *json, char *out, size_t capacity)
{
  if (!out || !capacity) return -EINVAL;
  out[0] = 0;
  if (!json || strnlen(json, 128 * 1024 + 1) > 128 * 1024) return -EFBIG;
  if (strstr(json, "\\u0000")) return -EBADMSG;
  cJSON *root = cJSON_ParseWithOpts(json, NULL, 1);
  if (!root) return -EBADMSG;
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "text");
  int ret = -EBADMSG;
  if (cJSON_IsObject(root) && !cJSON_GetObjectItemCaseSensitive(root, "error") &&
      cJSON_IsString(value) && claw_voice_valid_text(value->valuestring, capacity - 1)) {
    const char *s = value->valuestring;
    while (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r') s++;
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\n' || s[n-1] == '\t' || s[n-1] == '\r')) n--;
    if (n) { memcpy(out, s, n); out[n] = 0; ret = 0; }
    else ret = -ENODATA;
  }
  cJSON_Delete(root);
  return ret;
}
