/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_weather.h"
#include "cJSON.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bound recursive parser stack use before calling cJSON on network data. */
static bool shallow_json(const char *text, size_t size)
{
  unsigned depth = 0;
  bool quoted = false, escaped = false;
  for (size_t i = 0; i < size; i++)
    {
      unsigned char c = text[i];
      if (!c) return false;
      if (quoted)
        {
          if (escaped) escaped = false;
          else if (c == '\\') escaped = true;
          else if (c == '"') quoted = false;
        }
      else if (c == '"') quoted = true;
      else if (c == '{' || c == '[')
        { if (++depth > 8) return false; }
      else if (c == '}' || c == ']')
        { if (!depth) return false; depth--; }
    }
  return !depth && !quoted;
}

static int number(const cJSON *object, const char *key, int low, int high,
                  int missing, int *out)
{
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
  *out = missing;
  if (!value || cJSON_IsNull(value)) return 0;
  if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
      value->valuedouble < low || value->valuedouble > high) return -EINVAL;
  double n = value->valuedouble;
  *out = (int)(n < 0 ? n - .5 : n + .5);
  return 0;
}

/* Never split UTF-8 or copy control characters into LVGL labels. */
static void label(const cJSON *object, const char *key, char *out, size_t size)
{
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
  out[0] = 0;
  if (!cJSON_IsString(value)) return;
  const unsigned char *p = (const unsigned char *)value->valuestring;
  size_t used = 0;
  while (*p)
    {
      unsigned n = *p < 0x80 ? 1 : *p >= 0xc2 && *p <= 0xdf ? 2 :
                   *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 0;
      if (!n || *p < 0x20 || *p == 0x7f || used + n >= size) break;
      for (unsigned i = 1; i < n; i++)
        if (!p[i] || (p[i] & 0xc0) != 0x80) return;
      if ((n == 3 && ((p[0] == 0xe0 && p[1] < 0xa0) ||
                      (p[0] == 0xed && p[1] >= 0xa0))) ||
          (n == 4 && ((p[0] == 0xf0 && p[1] < 0x90) ||
                      (p[0] == 0xf4 && p[1] >= 0x90)))) break;
      memcpy(out + used, p, n);
      used += n;
      out[used] = 0;
      p += n;
    }
}

int glass_weather_parse(const char *json, size_t size,
                        struct glass_weather_data *out)
{
  if (!json || !out || !size || size > GLASS_WEATHER_BODY_MAX ||
      !shallow_json(json, size)) return -EINVAL;
  /* This pinned cJSON predates ParseWithLengthOpts. Give it an owned,
   * terminated buffer and require consumption of the entire document. */
  char *text = malloc(size + 1);
  if (!text) return -ENOMEM;
  memcpy(text, json, size);
  text[size] = 0;
  cJSON *root = cJSON_ParseWithOpts(text, NULL, true);
  free(text);
  if (!root) return -EINVAL;
  struct glass_weather_data data = {0};
  int ret = -EINVAL;
  if (!cJSON_IsObject(root)) goto done;
  if (number(root, "temperature", -100, 80, -999, &data.temperature) ||
      number(root, "humidity", 0, 100, -1, &data.humidity) ||
      number(root, "aqi", 0, 1000, -1, &data.aqi) ||
      number(root, "aqi_level", 1, 6, 0, &data.level)) goto done;
  const cJSON *pollutants = cJSON_GetObjectItemCaseSensitive(root, "air_pollutants");
  if (pollutants && !cJSON_IsNull(pollutants) && !cJSON_IsObject(pollutants)) goto done;
  if (number(pollutants, "pm25", 0, 5000, -1, &data.pm25)) goto done;
  label(root, "city", data.location, sizeof(data.location));
  if (!data.location[0]) label(root, "province", data.location, sizeof(data.location));
  label(root, "weather", data.condition, sizeof(data.condition));
  label(root, "aqi_category", data.category, sizeof(data.category));
  label(root, "report_time", data.report_time, sizeof(data.report_time));
  if (!data.location[0] ||
      (data.temperature == -999 && data.humidity < 0 && data.aqi < 0)) goto done;
  if (data.aqi >= 0 && !data.level)
    data.level = data.aqi <= 50 ? 1 : data.aqi <= 100 ? 2 : data.aqi <= 150 ? 3 :
                 data.aqi <= 200 ? 4 : data.aqi <= 300 ? 5 : 6;
  if (!data.category[0] && data.level)
    {
      static const char *categories[] = {"", "优", "良", "轻度污染", "中度污染", "重度污染", "严重污染"};
      snprintf(data.category, sizeof(data.category), "%s", categories[data.level]);
    }
  *out = data;
  ret = 0;
done:
  cJSON_Delete(root);
  return ret;
}
