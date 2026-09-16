/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_WEATHER_H
#define GLASS_WEATHER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GLASS_WEATHER_BODY_MAX 4096
enum glass_weather_state
{
  WEATHER_WAIT_NET, WEATHER_WAIT_TIME, WEATHER_FETCHING,
  WEATHER_READY, WEATHER_FAILED
};

struct glass_weather_data
{
  int temperature;
  int humidity;
  int aqi;
  int level;
  int pm25;
  char location[64];
  char condition[32];
  char category[32];
  char report_time[64];
};

struct glass_weather_snapshot
{
  struct glass_weather_data data;
  enum glass_weather_state state;
  uint64_t received_ms;
  unsigned revision;
  int error;
  bool valid;
};

int glass_weather_parse(const char *json, size_t size,
                        struct glass_weather_data *out);
int glass_weather_fetch(struct glass_weather_data *out);
struct webclient_tls_ops;
int glass_weather_http(struct glass_weather_data *out,
                        const struct webclient_tls_ops *tls, void *tls_context);
void glass_weather_start(void);
void glass_weather_get(struct glass_weather_snapshot *out);
void glass_weather_refresh(void);
#endif
