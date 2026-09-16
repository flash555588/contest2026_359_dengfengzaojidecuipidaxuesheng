/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_weather.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct glass_weather_data parse(const char *s)
{
  struct glass_weather_data data;
  assert(glass_weather_parse(s, strlen(s), &data) == 0);
  return data;
}

static void rejected(const char *s, size_t size)
{
  struct glass_weather_data data, before;
  memset(&data, 0x5a, sizeof(data));
  before = data;
  assert(glass_weather_parse(s, size, &data) < 0);
  assert(!memcmp(&data, &before, sizeof(data)));
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  FILE *file = fopen(argv[1], "rb");
  assert(file);
  char buffer[GLASS_WEATHER_BODY_MAX + 2];
  size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
  fclose(file);
  buffer[size] = 0;
  struct glass_weather_data live = parse(buffer);
  assert(live.temperature != -999 && live.humidity >= 0);
  assert(live.aqi >= 0 && live.location[0]);
  const char *sample = "{\"city\":\"成都市\",\"temperature\":18,\"humidity\":93,\"aqi\":14,\"air_pollutants\":{\"pm25\":5}}";
  struct glass_weather_data data = parse(sample);
  assert(data.aqi == 14 && data.pm25 == 5 && data.humidity == 93);
  assert(!strcmp(data.location, "成都市") && !strcmp(data.category, "优"));
  data = parse("{\"city\":\"北京\",\"temperature\":0,\"humidity\":0,\"aqi\":0}");
  assert(data.temperature == 0 && data.humidity == 0 && data.aqi == 0);
  data = parse("{\"city\":\"北京\",\"temperature\":-2.6,\"humidity\":null,\"aqi\":null}");
  assert(data.temperature == -3 && data.humidity == -1 && data.aqi == -1 && data.pm25 == -1);
  data = parse("{\"city\":\"成都\\u5e02\",\"humidity\":50} \r\n");
  assert(!strcmp(data.location, "成都市"));
  const char *bad[] = {"{}", "[]", "{\"error\":\"quota\"}", "null", "",
    "{\"city\":\"a\",\"aqi\":-1}", "{\"city\":\"a\",\"humidity\":101}",
    "{\"city\":\"a\",\"temperature\":\"18\"}", "{\"city\":\"a\",\"temperature\":1e999}",
    "{\"city\":\"a\",\"temperature\":18,\"aqi_level\":7}",
    "{\"city\":\"a\",\"temperature\":18} garbage",
    "{\"city\":\"a\",\"temperature\":18}{}",
    "[[[[[[[[[0]]]]]]]]]"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) rejected(bad[i], strlen(bad[i]));
  for (size_t i = 0; i < strlen(sample); i++) rejected(sample, i);
  char big[5000];
  memset(big, ' ', sizeof(big));
  rejected(big, sizeof(big));
  const char embedded[] = "{\"city\":\"a\",\"aqi\":3}\0{}";
  rejected(embedded, sizeof(embedded) - 1);
  char longcity[1024] = "{\"city\":\"";
  for (int i = 0; i < 100; i++) strcat(longcity, "成");
  strcat(longcity, "\",\"temperature\":18}");
  data = parse(longcity);
  assert(strlen(data.location) == 63);
  unsigned seed = 12345;
  for (unsigned test = 0; test < 10000; test++)
    {
      char fuzz[512];
      size_t length = test % sizeof(fuzz);
      for (size_t i = 0; i < length; i++)
        { seed = seed * 1664525 + 1013904223; fuzz[i] = seed >> 24; }
      glass_weather_parse(fuzz, length, &data);
    }
  puts("PASS: live API JSON, AQI vs PM2.5, missing/zero values, UTF-8, truncation, size/depth limits and 10000 malformed inputs");
  return 0;
}
