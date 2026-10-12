/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_api.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
static const cJSON *get(const cJSON *o, const char *key)
{
  return cJSON_GetObjectItemCaseSensitive(o, key);
}
static int number(const cJSON *o, const char *key, double *v)
{
  const cJSON *p = get(o, key);
  if (!cJSON_IsNumber(p) || !isfinite(p->valuedouble)) return -EPROTO;
  *v = p->valuedouble; return 0;
}
static const char *string(const cJSON *o, const char *key)
{
  const cJSON *p = get(o, key);
  return cJSON_IsString(p) ? p->valuestring : NULL;
}
static cJSON *response(const char *json)
{
  double code;
  cJSON *o = cJSON_ParseWithOpts(json, NULL, 1);
  if (!o) return NULL;
  if (number(o, "code", &code) || code != 0) {
    cJSON_Delete(o); return NULL;
  }
  return o;
}
static int line_rank(const void *a, const void *b)
{
  return (strstr((const char *)a, "mcdn") != NULL) -
         (strstr((const char *)b, "mcdn") != NULL);
}
int bili_valid_room(const char *text)
{
  size_t i, n = strlen(text);
  if (!n || n > 10 || text[0] == '0') return 0;
  for (i = 0; i < n; i++) if (text[i] < '0' || text[i] > '9') return 0;
  return 1;
}
int bili_room_id(const char *json, unsigned long *id)
{
  cJSON *o = response(json);
  const cJSON *data;
  double room, status;
  int r = -EPROTO;
  if (!o) return r;
  data = get(o, "data");
  if (!number(data, "room_id", &room) && room > 0 && room <= 4294967295.0 &&
      floor(room) == room && !number(data, "live_status", &status)) {
    if (status != 1) r = -ENODATA;
    else { *id = (unsigned long)room; r = 0; }
  }
  cJSON_Delete(o); return r;
}
int bili_play_info(const char *json, struct bili_streams *out)
{
  cJSON *o = response(json);
  const cJSON *streams, *s, *f, *c, *u;
  int r = -ENOTSUP;
  memset(out, 0, sizeof(*out));
  if (!o) return -EPROTO;
  streams = get(get(get(get(o, "data"), "playurl_info"), "playurl"), "stream");
  cJSON_ArrayForEach(s, streams) {
    const char *protocol = string(s, "protocol_name");
    if (!protocol || strcmp(protocol, "http_stream")) continue;
    cJSON_ArrayForEach(f, get(s, "format")) {
      const char *format = string(f, "format_name");
      if (!format || strcmp(format, "flv")) continue;
      cJSON_ArrayForEach(c, get(f, "codec")) {
        const char *codec = string(c, "codec_name"), *base = string(c, "base_url");
        double qn;
        if (!codec || strcmp(codec, "avc") || !base || base[0] != '/' ||
            number(c, "current_qn", &qn) || qn < 1 || qn > 100000 || floor(qn) != qn)
          continue;
        if (out->count && out->qn != (int)qn) continue;
        cJSON_ArrayForEach(u, get(c, "url_info")) {
          const char *host = string(u, "host"), *extra = string(u, "extra");
          char url[BILI_URL_MAX];
          int n, i;
          if (!host || !extra ||
              (strncmp(host, "https://", 8) && strncmp(host, "http://", 7))) continue;
          n = snprintf(url, sizeof(url), "%s%s%s", host, base, extra);
          if (n < 0 || n >= (int)sizeof(url) || strpbrk(url, "\r\n")) continue;
          for (i = 0; i < out->count; i++) if (!strcmp(url, out->urls[i])) break;
          if (i != out->count || out->count == BILI_MAX_LINES) continue;
          strcpy(out->urls[out->count++], url);
          out->qn = (int)qn;
        }
      }
    }
  }
  if (out->count) {
    qsort(out->urls, out->count, sizeof(out->urls[0]), line_rank);
    r = 0;
  }
  cJSON_Delete(o); return r;
}
