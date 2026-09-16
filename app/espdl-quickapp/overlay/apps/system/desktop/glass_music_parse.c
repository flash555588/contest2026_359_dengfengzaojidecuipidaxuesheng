/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_music.h"
#include <cJSON.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int copy_field(const cJSON *root, const char *key, char *out, size_t cap, bool required)
{
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!value || cJSON_IsNull(value)) return required ? -ENODATA : 0;
  if (!cJSON_IsString(value) || !value->valuestring) return -EINVAL;
  size_t n = strlen(value->valuestring);
  if (n >= cap) return -E2BIG;
  for (size_t i = 0; i < n; i++) if ((unsigned char)value->valuestring[i] < 32) return -EINVAL;
  memcpy(out, value->valuestring, n + 1);
  return required && !n ? -ENODATA : 0;
}

int music_song_parse(const char *body, size_t size, const char *query, struct music_song *song)
{
  if (!body || !song || !query || !size || size > 16384 || strlen(query) >= sizeof(song->query)) return -EINVAL;
  unsigned depth = 0; bool quoted = false, escaped = false;
  for (size_t i = 0; i < size; i++) {
    char c = body[i];
    if (!c) return -EINVAL;
    if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; }
    else if (c == '"') quoted = true;
    else if (c == '{' || c == '[') { if (++depth > 12) return -E2BIG; }
    else if (c == '}' || c == ']') { if (!depth) return -EINVAL; depth--; }
  }
  const char *end = NULL;
  char *copy = malloc(size + 1);
  if (!copy) return -ENOMEM;
  memcpy(copy, body, size); copy[size] = 0;
  cJSON *root = cJSON_ParseWithOpts(copy, &end, true);
  if (!root) { free(copy); return -EINVAL; }
  int ret = -EINVAL;
  const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
  if (!cJSON_IsObject(root)) goto done;
  if (!cJSON_IsNumber(code) || code->valuedouble != 1) { ret = -ENOENT; goto done; }
  struct music_song parsed = {0};
  ret = copy_field(root, "name", parsed.name, sizeof(parsed.name), true);
  if (!ret) ret = copy_field(root, "artist", parsed.artist, sizeof(parsed.artist), false);
  if (!ret) ret = copy_field(root, "album", parsed.album, sizeof(parsed.album), false);
  if (!ret) ret = copy_field(root, "music_url", parsed.url, sizeof(parsed.url), true);
  if (!ret && strncmp(parsed.url, "https://", 8)) ret = -EPROTONOSUPPORT;
  if (!ret) { strcpy(parsed.query, query); *song = parsed; }
done:
  cJSON_Delete(root);
  free(copy);
  return ret;
}

static int encode(const char *text, char *out, size_t cap)
{
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                 (*p >= '0' && *p <= '9') || strchr("-_.~", *p);
    if (n + (plain ? 1 : 3) >= cap) return -E2BIG;
    if (plain) out[n++] = *p;
    else { out[n++] = '%'; out[n++] = hex[*p >> 4]; out[n++] = hex[*p & 15]; }
  }
  out[n] = 0;
  return (int)n;
}

int music_search_url(const char *query, const char *key, char *url, size_t cap)
{
  if (!query || !query[0] || strlen(query) >= 128 || !key || !key[0]) return -EINVAL;
  int n = snprintf(url, cap, "https://jkapi.com/api/music?plat=qq&type=json&apiKey=");
  if (n < 0 || (size_t)n >= cap) return -E2BIG;
  int k = encode(key, url + n, cap - n);
  if (k < 0) return k;
  n += k;
  if ((size_t)n + 7 >= cap) return -E2BIG;
  memcpy(url + n, "&name=", 6); n += 6;
  k = encode(query, url + n, cap - n);
  return k < 0 ? k : 0;
}
